#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <functional>

#include "helpers.hpp"
#include "measurements.hpp"
#include "weighted_measurements.hpp"


extern "C" {
#include <fftw3.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <Eigen/Dense>
#include <Eigen/QR>

static const double pi = 3.141592653589793238462643383279502884197;

class Configuration {
	private:
	int L; // size of the system
	int D; // dimension
	int V; // volume of the system
	int N; // number of time-steps
	double beta; // inverse temperature
	double dt; // time step 
	double g; // interaction strength
	double mu; // chemical potential
	double A; // sqrt(exp(g*dt)-1)
	double B; // magnetic field
	double t; // nearest neighbour hopping
	double J; // next-nearest neighbour hopping

	std::vector<Eigen::VectorXd> diagonals;

	std::default_random_engine generator;
	std::bernoulli_distribution distribution;
	std::uniform_int_distribution<int> randomPosition;
	std::uniform_int_distribution<int> randomTime;
	std::exponential_distribution<double> trialDistribution;

	Eigen::VectorXd energies;
	Eigen::VectorXd freePropagator;
	Eigen::VectorXd freePropagator_b;

	Eigen::MatrixXd positionSpace; // current matrix in position space
	Eigen::MatrixXcd momentumSpace;

	fftw_plan x2p_col;
	fftw_plan p2x_col;

	fftw_plan x2p_row;
	fftw_plan p2x_row;

	double plog;

	mymeasurement<double> m_dens;
	mymeasurement<double> m_magn;

	public:

	Eigen::MatrixXd U_s;
	Eigen::VectorXcd ev_s;

	std::vector<double> fields;
	std::vector<weighted_measurement<double>> densities;
	std::vector<weighted_measurement<double>> magnetizations;

	public:

	void init () {
		V = std::pow(L, D);
		dt = beta/N;
		A = sqrt(exp(g*dt)-1.0);
		if (L==1) t = 0.0;
		auto distributor = std::bind(distribution, generator);
		diagonals.insert(diagonals.begin(), N, Eigen::VectorXd::Zero(V));
		for (int i=0;i<diagonals.size();i++) {
			for (int j=0;j<V;j++) {
				diagonals[i][j] = distributor()?A:-A;
			}
		}
		positionSpace = Eigen::MatrixXd::Identity(V, V);
		momentumSpace = Eigen::MatrixXcd::Identity(V, V);

		const int size[] = { L, L, L, };
		x2p_col = fftw_plan_many_dft_r2c(D, size, V, positionSpace.data(),
				NULL, 1, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, 1, V, FFTW_PATIENT);
		p2x_col = fftw_plan_many_dft_c2r(D, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, 1, V, positionSpace.data(), NULL, 1, V, FFTW_PATIENT);
		x2p_row = fftw_plan_many_dft_r2c(D, size, V, positionSpace.data(),
				NULL, V, 1, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, V, 1, FFTW_PATIENT);
		p2x_row = fftw_plan_many_dft_c2r(D, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, V, 1, positionSpace.data(), NULL, V, 1, FFTW_PATIENT);

		positionSpace = Eigen::MatrixXd::Identity(V, V);
		momentumSpace = Eigen::MatrixXcd::Identity(V, V);

		energies = Eigen::VectorXd::Zero(V);
		freePropagator = Eigen::VectorXd::Zero(V);
		freePropagator_b = Eigen::VectorXd::Zero(V);
		for (int i=0;i<V;i++) {
			energies[i] += -2.0 * t * ( cos(2.0*(i%L)*pi/L) - cos(2.0*((i/L)%L)*pi/L) - cos(2.0*(i/L/L)*pi/L) + (3.0-D) );
			energies[i] += -2.0 * J * ( cos(4.0*(i%L)*pi/L) - cos(4.0*((i/L)%L)*pi/L) - cos(4.0*(i/L/L)*pi/L) + (3.0-D) );
			freePropagator[i] = exp(-dt*energies[i]);
			freePropagator_b[i] = exp(dt*energies[i]);
		}

		plog = logProbability();

		for (int i=-15;i<30;i++) {
			double f = B + double(i)/10.0;
			fields.push_back(f);
			densities.push_back(weighted_measurement<double>());
			magnetizations.push_back(weighted_measurement<double>());
		}
	}

	Configuration (lua_State *L, int index) : distribution(0.5), trialDistribution(1.0) {
		lua_getfield(L, index, "L");  this->L = lua_tointeger(L, -1);        lua_pop(L, 1);
		lua_getfield(L, index, "D");  D = lua_tointeger(L, -1);        lua_pop(L, 1);
		lua_getfield(L, index, "N");  N = lua_tointeger(L, -1);        lua_pop(L, 1);
		lua_getfield(L, index, "T");  beta = 1.0/lua_tonumber(L, -1);  lua_pop(L, 1);
		lua_getfield(L, index, "t");  t = lua_tonumber(L, -1);         lua_pop(L, 1);
		lua_getfield(L, index, "U");  g = -lua_tonumber(L, -1);        lua_pop(L, 1); // FIXME: check this // should be right as seen in A above
		lua_getfield(L, index, "mu"); mu = lua_tonumber(L, -1);        lua_pop(L, 1);
		lua_getfield(L, index, "B");  B = lua_tonumber(L, -1);         lua_pop(L, 1);
		init();
	}

	void accumulate_forward (int start = 0, int end = -1) {
		double X = sqrt(1.0 - A*A);
		positionSpace.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			positionSpace.applyOnTheLeft((Eigen::VectorXd::Constant(V, 1.0)+diagonals[i]).asDiagonal());
			fftw_execute(x2p_col);
			momentumSpace.applyOnTheLeft(freePropagator.asDiagonal());
			fftw_execute(p2x_col);
			positionSpace /= V*X;
		}
	}

	void accumulate_backward (int start = 0, int end = -1) {
		double X = sqrt(1.0 - A*A);
		positionSpace.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			positionSpace.applyOnTheRight((Eigen::VectorXd::Constant(V, 1.0)-diagonals[i]).asDiagonal());
			fftw_execute(x2p_row);
			momentumSpace.applyOnTheRight(freePropagator_b.asDiagonal());
			fftw_execute(p2x_row);
			positionSpace /= V*X;
		}
	}

	double logProbability_simple () {
		double X = 1.0 - A*A;
		double Y = pow(X, N);
		accumulate_forward(0, N);
		Eigen::MatrixXd U_s = positionSpace;
		accumulate_backward(0, N);
		std::cout << std::endl;
		std::cout << N << std::endl;
		std::cout << X << std::endl;
		std::cout << Y << std::endl;
		std::cout << U_s*positionSpace << std::endl << std::endl;
		std::cout << positionSpace*U_s << std::endl << std::endl;
		std::cout << U_s.eigenvalues().transpose() << std::endl;
		std::cout << positionSpace.eigenvalues().transpose() << std::endl;
		std::cout << positionSpace.eigenvalues().array().inverse().transpose() << std::endl;
		std::cout << std::endl;
	}

	double logProbability_complex () {
		const int M = 30;
		std::vector<Eigen::MatrixXd> fvec;
		std::vector<Eigen::MatrixXd> bvec;
		for (int i=0;i<N;i+=M) {
			accumulate_forward(i, i+M);
			fvec.push_back(positionSpace);
		}
		for (int i=0;i<N;i+=M) {
			accumulate_backward(i, i+M);
			bvec.push_back(positionSpace);
		}
		test_sequences(fvec, bvec);
	}

	double logProbability () {
		positionSpace.setIdentity(V, V);
		for (int i=0;i<N;i++) {
			positionSpace.applyOnTheLeft((Eigen::VectorXd::Constant(V, 1.0)+diagonals[i]).asDiagonal());
			fftw_execute(x2p_col);
			momentumSpace.applyOnTheLeft(freePropagator.asDiagonal());
			fftw_execute(p2x_col);
			positionSpace /= V;
		}
		Eigen::VectorXcd eva;
		Eigen::VectorXd evb = Eigen::VectorXd::Ones(V);
		//dggev(positionSpace, Eigen::MatrixXd::Identity(V, V), eva, evb);
		eva = positionSpace.eigenvalues();

		std::complex<double> ret = 0.0;
		ret += (evb.cast<std::complex<double>>() + std::exp(+beta*B*0.5+beta*mu)*eva).array().log().sum();
		ret -= evb.array().log().sum();
		ret += (evb.cast<std::complex<double>>() + std::exp(-beta*B*0.5+beta*mu)*eva).array().log().sum();
		ret -= evb.array().log().sum();

		//for (int i=0;i<V;i++) {
			//if (std::abs(eva[i].imag())<1e-10 && eva[i].real()<0.0) {
				//std::cout << i << ' ' << eva[i] << std::endl;
				//std::cout << eva.transpose() << std::endl;
				//logProbability_complex();
				//throw("wtf");
			//}
		//}

		if (std::cos(ret.imag())<0.99) {
			logProbability_complex();
			throw("wtf");
		}

		return ret.real();
	}

	void print () {
		for (int i=0;i<N;i++) {
			for (int j=0;j<V;j++) {
				std::cout << (diagonals[i][j]<0?'-':'+');
			}
			std::cout << std::endl;
		}
	}

	bool metropolis (int M = 0) {
		if (M==0) M = 0.1 * volume() * N;
		bool ret = false;
		std::vector<int> index(M);
		for (int j=0;j<M;j++) {
			std::uniform_int_distribution<int> distr(0, V*N-j-1);
			int x = distr(generator);
			int i = j;
			// standard insertion sort would be:
			//
			// while (i>0 && x<index[i-1]) { index[i] = index[i-1]; i--;}
			//
			// if we are going to insert at place i we have to shift the value by i
			// if this number is less *or equal* than the one below in the list,
			// we cannot insert (we want unique indices) so we shift i down and move
			// up by one the top index (in the standard insertion sort equality is
			// irrelevant)
			while (i>0 && x+i<=index[i-1]) { index[i] = index[i-1]; i--; }
			index[i] = x+i;
		}
		for (int i=0;i<M;i++) {
			int t = index[i]/V;
			int x = index[i]%V;
			diagonals[t][x] = -diagonals[t][x];
		}
		double trial = logProbability();
		//logProbability_complex();
		//throw "end";
		if (-trialDistribution(generator)<trial-plog) {
			plog = trial;
			U_s = positionSpace;
			ev_s = positionSpace.eigenvalues();
			ret = true;
		} else {
			for (int i=0;i<M;i++) {
				int t = index[i]/V;
				int x = index[i]%V;
				diagonals[t][x] = -diagonals[t][x];
			}
			ret = false;
		}
		return ret;
	}

	double fraction_completed () const {
		return 1.0;
	}

	void update () {
		metropolis();
	}

	void extract_data (const Eigen::MatrixXd &M, Eigen::ArrayXd &d, Eigen::ArrayXd &d1, Eigen::ArrayXd &d2, double &K) {
		positionSpace = M;
		d = positionSpace.diagonal();
		d1.resize(positionSpace.rows());
		d2.resize(positionSpace.rows());
		// get super- and sub- diagonal
		for (int i=0;i<V;i++) {
			d1[i] = positionSpace(i, (i+1)%V);
			d2[i] = positionSpace((i+1)%V, i);
		}
		fftw_execute(x2p_col);
		positionSpace.applyOnTheLeft(energies.asDiagonal());
		fftw_execute(p2x_col);
		K = positionSpace.trace() / V;
	}

	void measure () {
		Eigen::ArrayXd d_up, d_dn;
		Eigen::ArrayXd d1_up, d1_dn;
		Eigen::ArrayXd d2_up, d2_dn;
		double K_up, K_dn;
		extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(+beta*B*0.5+beta*mu)*U_s).inverse(), d_up, d1_up, d2_up, K_up);
		extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(-beta*B*0.5+beta*mu)*U_s).inverse(), d_dn, d1_dn, d2_dn, K_dn);
		double n_up = d_up.sum();
		double n_dn = d_dn.sum();
		double n2 = (d_up*d_dn).sum();
		if (std::isnan(n_up) || std::isinf(n_up)) {
			std::cout << n_up << std::endl;
			std::cout << n_dn << std::endl;
			std::cout << positionSpace << std::endl << std::endl;
			std::cout << positionSpace.eigenvalues().transpose() << std::endl << std::endl;
			std::cout << (Eigen::MatrixXd::Identity(V, V) + exp(-beta*B*0.5) * positionSpace).inverse() << std::endl << std::endl;
			throw(9);
		}
		m_dens.add( (n_up + n_dn) / V );
		m_magn.add( (n_up - n_dn) / 2.0 / V );
		for (int i=0;i<fields.size();i++) {
			double B = fields[i];
			std::complex<double> ret = 0.0;
			ret += (1.0 + std::exp(+beta*B*0.5+beta*mu)*ev_s.array()).log().sum();
			ret += (1.0 + std::exp(-beta*B*0.5+beta*mu)*ev_s.array()).log().sum();
			Eigen::ArrayXd d_up = ( Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(+beta*B*0.5+beta*mu)*U_s).inverse() ).diagonal();
			Eigen::ArrayXd d_dn = ( Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(-beta*B*0.5+beta*mu)*U_s).inverse() ).diagonal();
			double n_up = d_up.sum();
			double n_dn = d_dn.sum();
			double n2 = (d_up*d_dn).sum();
			if (std::cos(ret.imag())<0.99 && std::cos(ret.imag())>0.01) {
				throw 1;
			}
			densities[i].add((n_up + n_dn) / V, std::exp(ret-plog).real());
			magnetizations[i].add((n_up - n_dn) / 2.0 / V, std::exp(ret-plog).real());
		}
	}

	int volume () { return V; }
	int timeSlices () { return N; }

	~Configuration () {
		std::ofstream out ("last_results", std::ios::app);
		out << "# T mu N \\Delta N^2 M \\Delta M^2" << std::endl;
		for (int i=0;i<fields.size();i++) {
			out << 1.0/(beta*t) << ' ' << 0.5*(fields[i]+g)/t
				<< ' ' << 1+2*(magnetizations[i].mean()) << ' ' << 4*magnetizations[i].variance()
				<< ' ' << 0.5*(densities[i].mean()-1.0) << ' ' << 0.25*densities[i].variance() << std::endl;
		}
		out << std::endl;
		fftw_destroy_plan(x2p_col);
		fftw_destroy_plan(p2x_col);
		fftw_destroy_plan(x2p_row);
		fftw_destroy_plan(p2x_row);
	}
	protected:
};

using namespace std;

int main (int argc, char **argv) {
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	luaL_dofile(L, argv[1]);

	for (int i=1;i<=lua_gettop(L);i++) { try {
		lua_getfield(L, i, "THERMALIZATION");
		int thermalization_sweeps = lua_tointeger(L, -1);
		lua_pop(L, i);
		lua_getfield(L, i, "SWEEPS");
		int total_sweeps = lua_tointeger(L, -1);
		lua_pop(L, i);

		Configuration configuration(L, i);

		int n = 0;
		int a = 0;
		int M = 1;
		for (int i=0;i<thermalization_sweeps;i++) {
			if (i%100==0) { std::cout << i << "\r"; std::cout.flush(); }
			if (configuration.metropolis(M)) a++;
			n++;
			if (i%200==0) {
				if (a>0.6*n && M<0.1*configuration.volume()*configuration.timeSlices()) {
					cout << "M: " << M;
					M += 5;
					cout << " -> " << M << endl;
					n = 0;
					a = 0;
					i = 0;
				} else if (a<0.4*n) {
					cout << "M: " << M;
					M -= 5;
					cout << " -> " << M << endl;
					M = M>0?M:1;
					n = 0;
					a = 0;
					i = 0;
				}
			}
		}
		std::cout << thermalization_sweeps << "\n"; std::cout.flush();
		for (int i=0;i<total_sweeps;i++) {
			if (i%100==0) { std::cout << i << "\r"; std::cout.flush(); }
			if (configuration.metropolis(M)) a++;
			n++;
			configuration.measure();
		}
		std::cout << total_sweeps << "\n"; std::cout.flush();
	} catch (...) {}
	}

	lua_close(L);
	return 0;
}

