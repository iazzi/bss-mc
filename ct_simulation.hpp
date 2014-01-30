#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include "config.hpp"

#include "svd.hpp"
#include "types.hpp"
#include "measurements.hpp"

#include <fstream>
#include <random>
#include <iostream>
#include <vector>
#include <map>

extern "C" {
#include <fftw3.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <Eigen/QR>

static const double pi = 3.141592653589793238462643383279502884197;

template <typename T> using mymeasurement = measurement<T, false>;

#if 0
static auto measurements_proto = make_named_tuple(
		named_value2(mymeasurement<double>(), acceptance),
		named_value2(mymeasurement<double>(), density),
		named_value2(mymeasurement<double>(), magnetization),
		named_value2(mymeasurement<double>(), order_parameter),
		named_value2(mymeasurement<double>(), chi_d),
		named_value2(mymeasurement<double>(), chi_af),
		named_value2(mymeasurement<double>(), kinetic),
		named_value2(mymeasurement<double>(), interaction),
		named_value2(mymeasurement<double>(), sign),
		named_value2(mymeasurement<double>(), measured_sign),
		named_value2(mymeasurement<double>(), sign_correlation)
		);
};

typedef decltype(measurements_proto) measurements_type;
#endif

class Simulation {
	private:

	// Model parameters
	config::hubbard_config config;
	int Lx, Ly, Lz; // size of the system
	int V; // volume of the system
	int N; // number of time-steps
	double beta; // inverse temperature
	double dt; // time step 
	double g; // interaction strength
	double mu; // chemical potential
	double A; // sqrt(exp(g*dt)-1)
	double B; // magnetic field
	double tx, ty, tz; // nearest neighbour hopping
	//double Vx, Vy, Vz; // trap strength
	//double staggered_field;


	//state
	std::vector<Vector_d> diagonals;

	// Monte Carlo scheme settings
	std::mt19937_64 generator;
	bool reset;
	std::string outfn;
	std::string gf_name;
	int mslices;
	int msvd;
	int flips_per_update;
	bool open_boundary;


	// RNG distributions
	std::bernoulli_distribution distribution;
	std::uniform_int_distribution<int> randomPosition;
	std::uniform_int_distribution<int> randomTime;
	std::exponential_distribution<double> trialDistribution;

	Vector_d freePropagator;
	Vector_d freePropagator_b;
	Matrix_d freePropagator_open;
	Matrix_d freePropagator_inverse;
	double w_x, w_y, w_z;
	Vector_d potential;
	Vector_d freePropagator_x;
	//Vector_d freePropagator_x_b;
	Array_d staggering;

	Matrix_d positionSpace; // current matrix in position space
	Matrix_cd momentumSpace; // current matrix in momentum space

	std::vector<Matrix_d> slices_up;
	std::vector<Matrix_d> slices_dn;
	std::vector<bool> valid_slices;

	double update_prob;
	double update_sign;
	int update_size;
	int new_update_size;
	//std::vector<bool> update_flips;
	Matrix_d update_U;
	Matrix_d update_Vt;
	std::vector<int> update_perm;
	Matrix_d update_matrix_up;
	Matrix_d update_matrix_dn;

	Matrix_d hamiltonian;
	Matrix_d eigenvectors;
	Array_d energies;

	public:

	SVDHelper svd;
	SVDHelper svdA;
	SVDHelper svdB;
	SVDHelper svd_inverse;
	SVDHelper svd_inverse_up;
	SVDHelper svd_inverse_dn;

	Vector_cd v_x;
	Vector_cd v_p;

	fftw_plan x2p_col;
	fftw_plan p2x_col;
	//fftw_plan x2p_row;
	//fftw_plan p2x_row;

	double plog;
	double psign;

	Matrix_d rho_up;
	Matrix_d rho_dn;

	struct {
		Vector_d u_smart;
		Vector_d v_smart;
	} cache;

	public:

	int steps;

	mymeasurement<double> acceptance;
	mymeasurement<double> density;
	mymeasurement<double> magnetization;
	mymeasurement<double> order_parameter;
	mymeasurement<double> chi_d;
	mymeasurement<double> chi_af;
	//measurement<double, false> magnetization_slow;
	mymeasurement<double> kinetic;
	mymeasurement<double> interaction;
	mymeasurement<double> sign;
	mymeasurement<double> measured_sign;
	//mymeasurement<double> sign_correlation;
	mymeasurement<double> exact_sign;
	std::vector<mymeasurement<double>> d_up;
	std::vector<mymeasurement<double>> d_dn;
	std::vector<mymeasurement<double>> spincorrelation;
	std::vector<mymeasurement<double>> error;
	// RNG distributions
	mymeasurement<double> staggered_magnetization;

	std::vector<mymeasurement<Eigen::ArrayXXd>> green_function_up;
	std::vector<mymeasurement<Eigen::ArrayXXd>> green_function_dn;

	int time_shift;

	int shift_x (int x, int k) {
		int a = (x/Ly/Lz)%Lx;
		int b = x%(Ly*Lz);
		return ((a+k+Lx)%Lx)*Ly*Lz + b;
	}

	int shift_y (int y, int k) {
		int a = (y/Lz)%Ly;
		int b = y-a*Lz;
		return ((a+k+Ly)%Ly)*Lz + b;
	}

	Vector_d& diagonal (int t) {
		return diagonals[(t+time_shift)%N];
	}

	const Vector_d& diagonal (int t) const {
		return diagonals[(t+time_shift)%N];
	}

	public:

	void prepare_propagators ();
	void prepare_open_boundaries ();

	void init_measurements () {
		sign.set_name("Sign");
		acceptance.set_name("Acceptance");
		density.set_name("Density");
		magnetization.set_name("Magnetization");
		order_parameter.set_name("Order Parameter");
		chi_d.set_name("Chi (D-wave)");
		chi_af.set_name("Chi (AF)");
		//measured_sign.set_name("Sign (Measurements)");
		//sign_correlation.set_name("Sign Correlation");
		exact_sign.set_name("Sign (Exact)");
		//magnetization_slow.set_name("Magnetization (slow)");
		for (int i=0;i<V;i++) {
			d_up.push_back(mymeasurement<double>());
			d_dn.push_back(mymeasurement<double>());
		}
		for (int i=0;i<V;i++) {
			spincorrelation.push_back(mymeasurement<double>());
		}
		for (int i=0;i<=N;i++) {
			error.push_back(mymeasurement<double>());
			green_function_up.push_back(mymeasurement<Eigen::ArrayXXd>());
			green_function_dn.push_back(mymeasurement<Eigen::ArrayXXd>());
		}
	}

	void reset_updates () {
		update_prob = 0.0;
		update_sign = 1.0;
		update_size = 0.0;
		update_perm.resize(V);
		for (int i=0;i<V;i++) update_perm[i] = i;
		//update_flips.resize(V);
		//for (bool& b : update_flips) b = false;
		update_U.setZero(V, V);
		update_Vt.setZero(V, V);
	}

	void init ();

	void load (lua_State *L, int index);
	void save (lua_State *L, int index);
	void load_checkpoint (lua_State *L);
	void save_checkpoint (lua_State *L);

	Simulation (lua_State *L, int index) : distribution(0.5), trialDistribution(1.0), steps(0) {
		load(L, index);
	}

	double logDetU_s (int x = -1, int t = -1) {
		int nspinup = 0;
		for (int i=0;i<N;i++) {
			for (int j=0;j<V;j++) {
				if (diagonals[i][j]>0.0) nspinup++;
			}
		}
		if (x>=0 && t>=0) {
			nspinup += diagonals[t][x]>0.0?-1:+1;
		}
		return nspinup*std::log(1.0+A) + (N*V-nspinup)*std::log(1.0-A);
	}

	int nslices () const { return N/mslices + ((N%mslices>0)?1:0); }

	void make_slice (int i) {
		if (!valid_slices[i/mslices]) {
			//std::cerr << "remaking slice " << i << " (" << i/mslices << ')' << std::endl;
			slices_up[i/mslices].setIdentity(V, V);
			slices_dn[i/mslices].setIdentity(V, V);
			accumulate_forward(i, i+mslices, slices_up[i/mslices], slices_dn[i/mslices]);
			valid_slices[i/mslices] = true;
		}
	}

	void make_slices () {
		slices_up.resize(N/mslices + ((N%mslices>0)?1:0));
		slices_dn.resize(N/mslices + ((N%mslices>0)?1:0));
		for (int i=0;i<N;i+=mslices) {
			if (!valid_slices[i/mslices]) {
				slices_up[i/mslices].setIdentity(V, V);
				slices_dn[i/mslices].setIdentity(V, V);
				accumulate_forward(i, i+mslices, slices_up[i/mslices], slices_dn[i/mslices]);
				valid_slices[i/mslices] = true;
			}
		}
	}

	void make_svd () {
	}

	void make_svd_double () {
		svdA.setIdentity(V);
		svdB.setIdentity(V);
		for (int i=0;i<N;) {
			svdA.U.applyOnTheLeft(((Vector_d::Constant(V, 1.0)+diagonal(i)).array()).matrix().asDiagonal());
			svdB.U.applyOnTheLeft(((Vector_d::Constant(V, 1.0)+diagonal(i)).array()).matrix().asDiagonal());
			if (true) {
				svdA.U.applyOnTheLeft(freePropagator_open);
				svdB.U.applyOnTheLeft(freePropagator_inverse);
			} else {
				svdA.U.applyOnTheLeft(freePropagator_x.asDiagonal());
				fftw_execute_dft_r2c(x2p_col, svdA.U.data(), reinterpret_cast<fftw_complex*>(momentumSpace.data()));
				momentumSpace.applyOnTheLeft((freePropagator/double(V)).asDiagonal());
				fftw_execute_dft_c2r(p2x_col, reinterpret_cast<fftw_complex*>(momentumSpace.data()), svdA.U.data());
				svdB.U.applyOnTheLeft(freePropagator_x.array().inverse().matrix().asDiagonal());
				fftw_execute_dft_r2c(x2p_col, svdB.U.data(), reinterpret_cast<fftw_complex*>(momentumSpace.data()));
				momentumSpace.applyOnTheLeft((freePropagator.array().inverse().matrix()/double(V)).asDiagonal());
				fftw_execute_dft_c2r(p2x_col, reinterpret_cast<fftw_complex*>(momentumSpace.data()), svdB.U.data());
			}
			i++;
			if (i%msvd==0 || i==N) {
				svdA.absorbU();
				svdB.absorbU();
			}
		}
		svdA.add_identity(std::exp(+beta*B*0.5+beta*mu));
		svdB.add_identity(std::exp(-beta*B*0.5+beta*mu));
	}

	void make_density_matrices () {
		make_svd();
		svdA = svd;
		svdA.add_identity(std::exp(+beta*B*0.5+beta*mu));
		svdB = svd;
		svdB.add_identity(std::exp(-beta*B*0.5+beta*mu));
	}

	void make_svd_inverse () {
		make_svd_double();
		svd_inverse_up = svdA;
		svd_inverse_up.invertInPlace();
		svd_inverse_dn = svdB;
		svd_inverse_dn.invertInPlace();
		update_matrix_up = -svd_inverse_up.matrix();
		update_matrix_up.diagonal() += Vector_d::Ones(V);
		update_matrix_up.applyOnTheLeft(-2.0*(diagonal(0).array().inverse()+1.0).inverse().matrix().asDiagonal());
		update_matrix_up.diagonal() += Vector_d::Ones(V);
		update_matrix_dn = -svd_inverse_dn.matrix();
		update_matrix_dn.diagonal() += Vector_d::Ones(V);
		update_matrix_dn.applyOnTheLeft(-2.0*(diagonal(0).array().inverse()+1.0).inverse().matrix().asDiagonal());
		update_matrix_dn.diagonal() += Vector_d::Ones(V);
	}

	double svd_probability () {
		double ret = svdA.S.array().log().sum() + svdB.S.array().log().sum();
		//std::cerr << svd.S.transpose() << std::endl;
		return ret; // * (svdA.U*svdA.Vt*svdB.U*svdB.Vt).determinant();
	}

	double svd_sign () {
		return (svdA.U*svdA.Vt*svdB.U*svdB.Vt).determinant()>0.0?1.0:-1.0;
	}

	void accumulate_forward (int start, int end, Matrix_d &G_up, Matrix_d &G_dn) {
		while (end>N) end -= N;
		for (int i=start;i<end;i++) {
			G_up.applyOnTheLeft(((Vector_d::Constant(V, 1.0)+diagonals[i]).array()).matrix().asDiagonal());
			if (false) {
				svdA.U.applyOnTheLeft(freePropagator_open);
			} else {
				G_up.applyOnTheLeft(freePropagator_x.asDiagonal());
				fftw_execute_dft_r2c(x2p_col, G_up.data(), reinterpret_cast<fftw_complex*>(momentumSpace.data()));
				momentumSpace.applyOnTheLeft((freePropagator/double(V)).asDiagonal());
				fftw_execute_dft_c2r(p2x_col, reinterpret_cast<fftw_complex*>(momentumSpace.data()), G_up.data());
			}
		}
		for (int i=start;i<end;i++) {
			G_dn.applyOnTheLeft(((Vector_d::Constant(V, 1.0)+diagonals[i]).array()).matrix().asDiagonal());
			if (false) {
				G_dn.applyOnTheLeft(freePropagator_open);
			} else {
				G_dn.applyOnTheLeft(freePropagator_x.array().inverse().matrix().asDiagonal());
				fftw_execute_dft_r2c(x2p_col, G_dn.data(), reinterpret_cast<fftw_complex*>(momentumSpace.data()));
				momentumSpace.applyOnTheLeft((freePropagator.array().inverse().matrix()/double(V)).asDiagonal());
				fftw_execute_dft_c2r(p2x_col, reinterpret_cast<fftw_complex*>(momentumSpace.data()), G_dn.data());
			}
		}
	}

	void compute_uv_f_short (int x, int t) {
		//std::cerr << cache.u_smart[0] << ' ' << cache.u_smart[x] << ' ' << -2*diagonal(t)[x]/(1.0+diagonal(t)[x]) << std::endl;
		cache.u_smart.setZero(V);
		cache.u_smart[x] = -2*diagonal(t)[x]/(1.0+diagonal(t)[x]);
		cache.v_smart.setZero(V);
		cache.v_smart[x] = 1.0;
	}

	void flip (int x) {
		for (int t=0;t<N;t++) diagonal(t)[x] = -diagonal(t)[x];
	}

	void flip (int t, int x) {
		diagonal(t)[x] = -diagonal(t)[x];
	}

	void flip (int t, const std::vector<int> &vec) {
		for (int x : vec) {
			diagonal(t)[x] = -diagonal(t)[x];
		}
	}

	void redo_all () {
		//make_svd();
		make_svd_inverse();
		//make_density_matrices(); // already called in make_svd_inverse
		double np = svd_probability();
		double ns = svd_sign();
		if (fabs(np-plog-update_prob)>1.0e-8 || psign*update_sign!=ns) {
			std::cerr << "redo " << plog+update_prob << " <> " << np << " ~~ " << np-plog-update_prob << '\t' << (psign*update_sign*ns) << std::endl;
			plog = np;
			psign = ns;
			//std::cerr << "    " << np-plog << " ~~ " << update_prob << std::endl;
		}
		plog = np;
		psign = ns;
		if (isnan(plog)) {
			std::cerr << "NaN found: restoring" << std::endl;
			//make_svd();
			make_svd_inverse();
			//make_density_matrices() // already called in make_svd_inverse;
			plog = svd_probability();
			psign = svd_sign();
		} else {
		}
		//recheck();
		reset_updates();
	}

	std::pair<double, double> rank1_probability (int x);

	bool metropolis ();

	void set_time_shift (int t) { time_shift = t%N; redo_all(); }
	bool shift_time () { 
		time_shift++;
		bool ret = time_shift>=N;
		if (ret) time_shift -= N;
		redo_all();
		return ret;
	}

	void test_wrap () {
		time_shift = 0;
		redo_all();
	}

	void load_sigma (lua_State *L, const char *fn);

	double fraction_completed () const {
		return 1.0;
	}

	void update () {
		valid_slices[time_shift/mslices] = false;
		for (int i=0;i<flips_per_update;i++) {
			collapse_updates();
			acceptance.add(metropolis()?1.0:0.0);
			measured_sign.add(psign*update_sign);
		}
		//time_shift = randomTime(generator);
		//redo_all();
		//try_site_flip();
		shift_time();
	}

	bool try_site_flip () {
		int x = randomPosition(generator);
		flip(x);
		//make_svd();
		make_svd_inverse();
		double np = svd_probability();
		bool ret = -trialDistribution(generator)<np-plog;
		if (ret) {
			std::cerr << "accepted site flip at " << x << std::endl;
			plog = np;
			psign = svd_sign();
		} else {
			std::cerr << "rejected site flip at " << x << std::endl;
			flip(x);
			//make_svd();
			make_svd_inverse();
		}
		return ret;
	}

	void get_green_function (double s = 1.0, int t0 = 0);

	bool collapse_updates () {
		if (update_size>=V) {
			plog += update_prob;
			psign *= update_sign;
			//make_svd();
			make_svd_inverse();
			double np = svd_probability();
			double ns = svd_sign();
			if (fabs(np-plog)>1.0e-8 || psign!=ns) {
				std::cerr << "collapse " << plog+update_prob << " <> " << np << " ~~ " << np-plog-update_prob << '\t' << (psign*update_sign*ns) << std::endl;
			}
			plog = np;
			psign = ns;
			reset_updates();
			return true;
		} else {
			return false;
		}
	}

	double get_kinetic_energy (const Matrix_d &M) {
		positionSpace = M;
		return positionSpace.trace() / V;
	}

	double pair_correlation (const Matrix_d& rho_up, const Matrix_d& rho_dn) {
		double ret = 0.0;
		for (int x=0;x<V;x++) {
			for (int y=0;y<V;y++) {
				double u = rho_up(x, y);
				double d = 0.0;
				d += rho_dn(shift_x(x, +1), shift_x(y, +1));
				d += rho_dn(shift_x(x, -1), shift_x(y, +1));
				d -= rho_dn(shift_y(x, +1), shift_x(y, +1));
				d -= rho_dn(shift_y(x, -1), shift_x(y, +1));
				d += rho_dn(shift_x(x, +1), shift_x(y, -1));
				d += rho_dn(shift_x(x, -1), shift_x(y, -1));
				d -= rho_dn(shift_y(x, +1), shift_x(y, -1));
				d -= rho_dn(shift_y(x, -1), shift_x(y, -1));
				d -= rho_dn(shift_x(x, +1), shift_y(y, +1));
				d -= rho_dn(shift_x(x, -1), shift_y(y, +1));
				d += rho_dn(shift_y(x, +1), shift_y(y, +1));
				d += rho_dn(shift_y(x, -1), shift_y(y, +1));
				d -= rho_dn(shift_x(x, +1), shift_y(y, -1));
				d -= rho_dn(shift_x(x, -1), shift_y(y, -1));
				d += rho_dn(shift_y(x, +1), shift_y(y, -1));
				d += rho_dn(shift_y(x, -1), shift_y(y, -1));
				ret += u*d;
			}
		}
		return ret / V / V;
	}



	void measure ();
	void measure_quick ();
	void measure_sign ();
	int volume () const { return V; }
	int timeSlices () const { return N; }

	void write_wavefunction (std::ostream &out);

	void output_sign () {
		std::ostringstream buf;
		buf << outfn << "_sign.dat";
		std::ofstream out(buf.str(), reset?std::ios::trunc:std::ios::app);
		out << "# " << params();
		out << 1.0/(beta*tx) << ' ' << 0.5*(B+g)/tx;
		//out << ' ' << measured_sign.mean() << ' ' << measured_sign.error();
		//out << ' ' << sign_correlation.mean() << ' ' << sign_correlation.error();
		//out << ' ' << exact_sign.mean() << ' ' << exact_sign.variance();
		//if (staggered_field!=0.0) out << ' ' << -staggered_magnetization.mean()/staggered_field << ' ' << staggered_magnetization.variance();
		for (int i=0;i<V;i++) {
			//out << ' ' << d_up[i].mean();
		}
		for (int i=0;i<V;i++) {
			//out << ' ' << d_dn[i].mean();
		}
		for (int i=1;i<=Lx/2;i++) {
			//out << ' ' << spincorrelation[i].mean()/V << ' ' << spincorrelation[i].variance();
		}
		out << std::endl << std::endl;
	}

	void output_results () {
		std::ostringstream buf;
		buf << outfn << "stablefast_U" << (g/tx) << "_T" << 1.0/(beta*tx) << '_' << Lx << 'x' << Ly << 'x' << Lz << ".dat";
		outfn = buf.str();
		std::ofstream out(buf.str(), reset?std::ios::trunc:std::ios::app);
		out << 1.0/(beta*tx) << ' ' << 0.5*(B+g)/tx
			<< ' ' << density.mean() << ' ' << density.error()
			<< ' ' << magnetization.mean() << ' ' << magnetization.error()
			//<< ' ' << acceptance.mean() << ' ' << acceptance.variance()
			<< ' ' << kinetic.mean() << ' ' << kinetic.error()
			<< ' ' << interaction.mean() << ' ' << interaction.error();
		out << ' ' << order_parameter.mean() << ' ' << order_parameter.error();
		out << ' ' << chi_af.mean() << ' ' << chi_af.error();
		//out << ' ' << chi_d.mean() << ' ' << chi_d.error();
		out << ' ' << exact_sign.mean() << ' ' << exact_sign.error();
		out << ' ' << sign.mean() << ' ' << sign.error();
		//if (staggered_field!=0.0) out << ' ' << -staggered_magnetization.mean()/staggered_field << ' ' << staggered_magnetization.variance();
		for (int i=0;i<V;i++) {
			//out << ' ' << d_up[i].mean();
		}
		for (int i=0;i<V;i++) {
			//out << ' ' << d_dn[i].mean();
		}
		for (int i=1;i<=Lx/2;i++) {
			//out << ' ' << spincorrelation[i].mean()/V << ' ' << spincorrelation[i].variance();
		}
		out << std::endl;
		//out << "+-" << std::endl << green_function_up[0].error() << std::endl << std::endl;
		//out << "Green Function Up (N-1)" << std::endl << green_function_up[N-1].mean() << std::endl << std::endl;
		//out << "+-" << std::endl << green_function_up[N-1].error() << std::endl << std::endl;
		//out << "Green Function Dn (0)" << std::endl << green_function_dn[0].mean() << std::endl << std::endl;
		//out << "+-" << std::endl << green_function_dn[0].error() << std::endl << std::endl;
		//out << "Green Function Dn (N-1)" << std::endl << green_function_dn[N-1].mean() << std::endl << std::endl;
		//out << "+-" << std::endl << green_function_dn[N-1].error() << std::endl << std::endl;
		write_green_function();
	}

	void write_green_function ();

	std::string params () {
		std::ostringstream buf;
		buf << config << std::endl;
		return buf.str();
	}

	~Simulation () {
		fftw_destroy_plan(x2p_col);
		fftw_destroy_plan(p2x_col);
	}

	std::pair<double, double> recheck ();
	void straighten_slices ();

	void discard_measurements () {
		acceptance.clear();
		density.clear();
		magnetization.clear();
		order_parameter.clear();
		chi_d.clear();
		chi_af.clear();
		kinetic.clear();
		interaction.clear();
		sign.clear();
		measured_sign.clear();
		exact_sign.clear();
		for (int i=0;i<V;i++) {
			d_up[i].clear();
			d_dn[i].clear();
			spincorrelation[i].clear();
		}
	}

	protected:
};


#endif // SIMULATION_HPP

