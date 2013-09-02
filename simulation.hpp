#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include "svd.hpp"
#include "types.hpp"
#include "measurements.hpp"

#include <fstream>
#include <random>
#include <iostream>

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

class Simulation {
	private:
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
	double Vx, Vy, Vz; // trap strength
	double staggered_field;
	std::vector<Vector_d> diagonals;

	//int update_start;
	//int update_end;

	std::mt19937_64 generator;
	std::bernoulli_distribution distribution;
	std::uniform_int_distribution<int> randomPosition;
	std::uniform_int_distribution<int> randomTime;
	std::uniform_int_distribution<int> randomStep;
	std::exponential_distribution<double> trialDistribution;

	Vector_d energies;
	Vector_d freePropagator;
	Vector_d freePropagator_b;
	Vector_d potential;
	Vector_d freePropagator_x;
	Vector_d freePropagator_x_b;
	Array_d staggering;

	Matrix_d positionSpace; // current matrix in position space
	Matrix_cd positionSpace_c; // current matrix in position space
	Matrix_cd momentumSpace;

	int mslices;
	std::vector<Matrix_d> slices;
	int flips_per_update;

	double update_prob;
	double update_sign;
	int update_size;
	int max_update_size;
	Matrix_d update_U;
	Matrix_d update_Vt;

	int msvd;
	SVDHelper svd;
	SVDHelper svdA;
	SVDHelper svdB;
	SVDHelper svd_inverse;
	SVDHelper svd_inverse_up;
	SVDHelper svd_inverse_dn;
	Matrix_d first_slice_inverse;

	Vector_cd v_x;
	Vector_cd v_p;

	fftw_plan x2p_vec;
	fftw_plan p2x_vec;

	fftw_plan x2p_col;
	fftw_plan p2x_col;

	fftw_plan x2p_row;
	fftw_plan p2x_row;

	double plog;
	double psign;

	bool reset;
	//int reweight;
	std::string outfn;
	//std::ofstream logfile;

	Matrix_d U_s_inv;

	Matrix_d rho_up;
	Matrix_d rho_dn;

	struct {
		double a;
		double b;
		//double c;
		Vector_d u;
		Vector_d v;
		Vector_d u_smart;
		Vector_d v_smart;
		Matrix_d A;
		Matrix_d B;
		//Matrix_d C;
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
	std::vector<mymeasurement<double>> d_up;
	std::vector<mymeasurement<double>> d_dn;
	std::vector<mymeasurement<double>> spincorrelation;
	std::vector<mymeasurement<double>> error;
	mymeasurement<double> staggered_magnetization;

	int last_t;
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

	void init_measurements () {
		sign.set_name("Sign");
		acceptance.set_name("Acceptance");
		density.set_name("Density");
		magnetization.set_name("Magnetization");
		order_parameter.set_name("Order Parameter");
		chi_d.set_name("Chi (D-wave)");
		chi_af.set_name("Chi (AF)");
		measured_sign.set_name("Sign (Measurements)");
		//magnetization_slow.set_name("Magnetization (slow)");
		for (int i=0;i<V;i++) {
			d_up.push_back(mymeasurement<double>());
			d_dn.push_back(mymeasurement<double>());
		}
		for (int i=0;i<=Lx/2;i++) {
			spincorrelation.push_back(mymeasurement<double>());
		}
		for (int i=0;i<N;i++) {
			error.push_back(mymeasurement<double>());
		}
	}

	void reset_updates () {
		update_prob = 0.0;
		update_sign = 1.0;
		update_size = 0.0;
		update_U.setZero(V, max_update_size);
		update_Vt.setZero(max_update_size, V);
	}

	void init () {
		if (Lx<2) { Lx = 1; tx = 0.0; }
		if (Ly<2) { Ly = 1; ty = 0.0; }
		if (Lz<2) { Lz = 1; tz = 0.0; }
		V = Lx * Ly * Lz;
		mslices = mslices>0?mslices:N;
		mslices = mslices<N?mslices:N;
		time_shift = 0;
		last_t = 0;
		if (max_update_size<1) max_update_size = 1;
		if (flips_per_update<1) flips_per_update = max_update_size;
		randomPosition = std::uniform_int_distribution<int>(0, V-1);
		randomTime = std::uniform_int_distribution<int>(0, N-1);
		randomStep = std::uniform_int_distribution<int>(0, mslices-1);
		dt = beta/N;
		A = sqrt(exp(g*dt)-1.0);
		diagonals.insert(diagonals.begin(), N, Vector_d::Zero(V));
		for (size_t i=0;i<diagonals.size();i++) {
			for (int j=0;j<V;j++) {
				diagonals[i][j] = distribution(generator)?A:-A;
				//diagonals[i][j] = i<N/4.9?-A:A;
			}
		}
		v_x = Vector_cd::Zero(V);
		v_p = Vector_cd::Zero(V);
		positionSpace.setIdentity(V, V);
		positionSpace_c.setIdentity(V, V);
		momentumSpace.setIdentity(V, V);

		const int size[] = { Lx, Ly, Lz, };
		x2p_vec = fftw_plan_dft(3, size, reinterpret_cast<fftw_complex*>(v_x.data()), reinterpret_cast<fftw_complex*>(v_p.data()), FFTW_FORWARD, FFTW_PATIENT);
		p2x_vec = fftw_plan_dft(3, size, reinterpret_cast<fftw_complex*>(v_p.data()), reinterpret_cast<fftw_complex*>(v_x.data()), FFTW_BACKWARD, FFTW_PATIENT);
		x2p_col = fftw_plan_many_dft(3, size, V, reinterpret_cast<fftw_complex*>(positionSpace_c.data()),
				NULL, 1, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, 1, V, FFTW_FORWARD, FFTW_PATIENT);
		p2x_col = fftw_plan_many_dft(3, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, 1, V, reinterpret_cast<fftw_complex*>(positionSpace_c.data()), NULL, 1, V, FFTW_BACKWARD, FFTW_PATIENT);
		x2p_row = fftw_plan_many_dft(3, size, V, reinterpret_cast<fftw_complex*>(positionSpace_c.data()),
				NULL, V, 1, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, V, 1, FFTW_FORWARD, FFTW_PATIENT);
		p2x_row = fftw_plan_many_dft(3, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, V, 1, reinterpret_cast<fftw_complex*>(positionSpace_c.data()), NULL, V, 1, FFTW_BACKWARD, FFTW_PATIENT);

		for (int l=0;l<0;l++) {
			Matrix_cd R = Matrix_cd::Random(V, V);
			positionSpace_c = R;
			fftw_execute(x2p_col);
			fftw_execute(p2x_col);
			positionSpace_c /= V;
			std::cerr << l << ' ' << (positionSpace_c-R).norm() << std::endl;
		}

		positionSpace.setIdentity(V, V);
		momentumSpace.setIdentity(V, V);

		energies = Vector_d::Zero(V);
		freePropagator = Vector_d::Zero(V);
		freePropagator_b = Vector_d::Zero(V);
		potential = Vector_d::Zero(V);
		freePropagator_x = Vector_d::Zero(V);
		freePropagator_x_b = Vector_d::Zero(V);
		staggering = Array_d::Zero(V);
		for (int i=0;i<V;i++) {
			int x = (i/Lz/Ly)%Lx;
			int y = (i/Lz)%Ly;
			int z = i%Lz;
			int Kx = Lx, Ky = Ly, Kz = Lz;
			int kx = (i/Kz/Ky)%Kx;
			int ky = (i/Kz)%Ky;
			int kz = i%Kz;
			energies[i] += -2.0 * ( tx * cos(2.0*kx*pi/Kx) + ty * cos(2.0*ky*pi/Ky) + tz * cos(2.0*kz*pi/Kz) );
			freePropagator[i] = exp(-dt*energies[i]);
			freePropagator_b[i] = exp(dt*energies[i]);
			potential[i] = (x+y+z)%2?-staggered_field:staggered_field;
			freePropagator_x[i] = exp(-dt*potential[i]);
			freePropagator_x_b[i] = exp(dt*potential[i]);
			staggering[i] = (x+y+z)%2?-1.0:1.0;
		}

		make_slices();
		make_svd();
		make_svd_inverse();
		make_density_matrices();
		plog = svd_probability();
		psign = svd_sign();

		init_measurements();
		reset_updates();
	}

	void load (lua_State *L, int index) {
		lua_getfield(L, index, "SEED");
		if (lua_isnumber(L, -1)) {
			generator.seed(lua_tointeger(L, -1));
		} else if (lua_isstring(L, -1)) {
			std::stringstream in(std::string(lua_tostring(L, -1)));
			in >> generator;
		}
		lua_pop(L, 1);
		lua_getfield(L, index, "Lx");   this->Lx = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "Ly");   this->Ly = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "Lz");   this->Lz = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "N");    N = lua_tointeger(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "T");    beta = 1.0/lua_tonumber(L, -1);            lua_pop(L, 1);
		lua_getfield(L, index, "tx");   tx = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "ty");   ty = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "tz");   tz = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "Vx");   Vx = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "Vy");   Vy = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "Vz");   Vz = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "U");    g = -lua_tonumber(L, -1);                  lua_pop(L, 1); // FIXME: check this // should be right as seen in A above
		lua_getfield(L, index, "mu");   mu = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "B");    B = lua_tonumber(L, -1);                   lua_pop(L, 1);
		lua_getfield(L, index, "h");    staggered_field = lua_tonumber(L, -1);     lua_pop(L, 1);
		lua_getfield(L, index, "RESET");  reset = lua_toboolean(L, -1);            lua_pop(L, 1);
		//lua_getfield(L, index, "REWEIGHT");  reweight = lua_tointeger(L, -1);      lua_pop(L, 1);
		lua_getfield(L, index, "OUTPUT");  outfn = lua_tostring(L, -1);            lua_pop(L, 1);
		lua_getfield(L, index, "SLICES");  mslices = lua_tointeger(L, -1);         lua_pop(L, 1);
		lua_getfield(L, index, "SVD");     msvd = lua_tointeger(L, -1);            lua_pop(L, 1);
		lua_getfield(L, index, "max_update_size");     max_update_size = lua_tointeger(L, -1);            lua_pop(L, 1);
		lua_getfield(L, index, "flips_per_update");     flips_per_update = lua_tointeger(L, -1);            lua_pop(L, 1);
		//lua_getfield(L, index, "update_start");     update_start = lua_tointeger(L, -1);         lua_pop(L, 1);
		//lua_getfield(L, index, "update_end");       update_end = lua_tointeger(L, -1);           lua_pop(L, 1);
		//lua_getfield(L, index, "LOGFILE");  logfile.open(lua_tostring(L, -1));     lua_pop(L, 1);
		init();
	}

	void save (lua_State *L, int index) {
		if (index<1) index = lua_gettop(L)+index;
		std::stringstream out;
		out << generator;
		lua_pushstring(L, out.str().c_str());
		lua_setfield(L, index, "SEED");
		lua_pushinteger(L, this->Lx); lua_setfield(L, index, "Lx");
		lua_pushinteger(L, this->Ly); lua_setfield(L, index, "Ly");
		lua_pushinteger(L, this->Lz); lua_setfield(L, index, "Lz");
		lua_pushinteger(L, N); lua_setfield(L, index, "N");
		lua_pushnumber(L, 1.0/beta); lua_setfield(L, index, "T");
		lua_pushnumber(L, tx); lua_setfield(L, index, "tx");
		lua_pushnumber(L, ty); lua_setfield(L, index, "ty");
		lua_pushnumber(L, tz); lua_setfield(L, index, "tz");
		lua_pushnumber(L, Vx); lua_setfield(L, index, "Vx");
		lua_pushnumber(L, Vy); lua_setfield(L, index, "Vy");
		lua_pushnumber(L, Vz); lua_setfield(L, index, "Vz");
		lua_pushnumber(L, -g); lua_setfield(L, index, "U");
		lua_pushnumber(L, mu); lua_setfield(L, index, "mu");
		lua_pushnumber(L, B); lua_setfield(L, index, "B");
		lua_pushnumber(L, staggered_field); lua_setfield(L, index, "h");
		lua_pushinteger(L, mslices); lua_setfield(L, index, "SLICES");
		lua_pushinteger(L, msvd); lua_setfield(L, index, "SVD");
		lua_pushinteger(L, max_update_size); lua_setfield(L, index, "max_update_size");
		lua_pushinteger(L, flips_per_update); lua_setfield(L, index, "flips_per_update");
		lua_newtable(L);
		L << sign;
		lua_setfield(L, -2, "sign");
		L << acceptance;
		lua_setfield(L, -2, "acceptance");
		L << density;
		lua_setfield(L, -2, "density");
		L << magnetization;
		lua_setfield(L, -2, "magnetization");
		L << order_parameter;
		lua_setfield(L, -2, "order_parameter");
		L << chi_af;
		lua_setfield(L, -2, "chi_af");
		L << measured_sign;
		lua_setfield(L, -2, "measured_sign");
		L << chi_d;
		lua_setfield(L, -2, "chi_d");
		lua_setfield(L, index, "results");
	}

	Simulation (lua_State *L, int index) : distribution(0.8), trialDistribution(1.0), steps(0) {
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

	void make_slices () {
		slices.clear();
		for (int i=0;i<N;i+=mslices) {
			accumulate_forward(i, i+mslices);
			slices.push_back(positionSpace);
		}
	}

	void make_svd () {
		svd.setIdentity(V);
		for (size_t i=0;i<slices.size();i++) {
			svd.U.applyOnTheLeft(slices[i]);
			if (i%msvd==0 || i==slices.size()-1) svd.absorbU();
		}
		//std::cerr << svd.S.array().log().sum() << ' ' << logDetU_s() << std::endl;
	}

	void make_density_matrices () {
		svdA = svd;
		svdA.add_identity(std::exp(+beta*B*0.5+beta*mu));
		svdB = svd;
		svdB.add_identity(std::exp(-beta*B*0.5+beta*mu));
	}

	void make_svd_inverse () {
		svd_inverse = svd;
		svd_inverse.invertInPlace();
		svd_inverse_up = svd_inverse;
		svd_inverse_up.add_identity(std::exp(-beta*B*0.5-beta*mu));
		svd_inverse_up.invertInPlace();
		svd_inverse_dn = svd_inverse;
		svd_inverse_dn.add_identity(std::exp(+beta*B*0.5-beta*mu));
		svd_inverse_dn.invertInPlace();
		first_slice_inverse = slices[0].inverse();
	}

	double svd_probability () {
		double ret = svdA.S.array().log().sum() + svdB.S.array().log().sum();
		//std::cerr << svd.S.transpose() << std::endl;
		return ret; // * (svdA.U*svdA.Vt*svdB.U*svdB.Vt).determinant();
	}

	double svd_sign () {
		return (svdA.U*svdA.Vt*svdB.U*svdB.Vt).determinant()>0.0?1.0:-1.0;
	}

	void accumulate_forward (int start = 0, int end = -1) {
		positionSpace_c.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			//std::cerr << "accumulate_f. " << i << " determinant = " << positionSpace_c.determinant() << std::endl;
			positionSpace_c.applyOnTheLeft(((Vector_d::Constant(V, 1.0)+diagonal(i)).array()*freePropagator_x.array()).matrix().asDiagonal());
			fftw_execute(x2p_col);
			momentumSpace.applyOnTheLeft(freePropagator.asDiagonal());
			fftw_execute(p2x_col);
			positionSpace_c /= V;
		}
		positionSpace = positionSpace_c.real();
	}

	void accumulate_backward (int start = 0, int end = -1) {
		Real X = 1.0 - A*A;
		positionSpace_c.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			positionSpace_c.applyOnTheRight(((Vector_d::Constant(V, 1.0)-diagonal(i)).array()*freePropagator_x_b.array()).matrix().asDiagonal());
			fftw_execute(x2p_row);
			momentumSpace.applyOnTheRight(freePropagator_b.asDiagonal());
			fftw_execute(p2x_row);
			positionSpace_c /= V*X;
		}
		positionSpace = positionSpace_c.real();
	}

	void compute_uv_f (int x, int t) {
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t+1;i<N;i++) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		fftw_execute(x2p_vec);
		v_p = v_p.array() * freePropagator.array();
		fftw_execute(p2x_vec);
		v_x /= V;
		cache.u = (-2*diagonal(t)[x]*v_x*freePropagator_x[x]).real();
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t-1;i>=0;i--) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		cache.v = v_x.real();
	}

	void compute_uv_f_short (int x, int t) {
		int start = mslices*(t/mslices);
		int end = mslices*(1+t/mslices);
		if (end>N) end = N;
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t+1;i<end;i++) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		fftw_execute(x2p_vec);
		v_p = v_p.array() * freePropagator.array();
		fftw_execute(p2x_vec);
		v_x /= V;
		cache.u_smart = (-2*diagonal(t)[x]*v_x*freePropagator_x[x]).real();
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t-1;i>=start;i--) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		cache.v_smart = v_x.real();
	}

	void compute_uv_f_smart (int x, int t) {
		int start = mslices*(t/mslices);
		int end = mslices*(1+t/mslices);
		if (end>N) end = N;
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t+1;i<end;i++) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		fftw_execute(x2p_vec);
		v_p = v_p.array() * freePropagator.array();
		fftw_execute(p2x_vec);
		v_x /= V;
		cache.u_smart = cache.u = (-2*diagonal(t)[x]*v_x*freePropagator_x[x]).real();
		for (size_t i=t/mslices+1;i<slices.size();i++) {
			//std::cerr << i << ' ' << t/mslices << ' ' << slices.size() << std::endl;
			cache.u.applyOnTheLeft(slices[i]);
		}
		v_x = Vector_cd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t-1;i>=start;i--) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Vector_d::Constant(V, 1.0)+diagonal(i)).array() * freePropagator_x.array();
			v_x /= V;
		}
		cache.v_smart = cache.v = v_x.real();
		for (int i=t/mslices-1;i>=0;i--) {
			cache.v.applyOnTheLeft(slices[i].transpose());
		}
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
		//make_slices();
		//int old_msvd = msvd;
		//msvd = 1;
		make_svd();
		//msvd = old_msvd;
		make_svd_inverse();
		make_density_matrices();
		double np = svd_probability();
		if (fabs(np-plog-update_prob)>1.0e-6) std::cerr << plog+update_prob << " <> " << np << " ~~ " << np-plog-update_prob << std::endl;
		//error[last_t].add(np-plog-update_prob);
		plog = np;
		psign = svd_sign();
		reset_updates();
	}

	std::pair<double, double> rank1_probability (int x, int t) { // FIXME: use SVD / higher beta
		compute_uv_f_short(x, t);
		const int L = update_size;
		update_U.col(L) = first_slice_inverse * cache.u_smart;
		update_Vt.row(L) = cache.v_smart.transpose();
		double d1 = ((update_Vt.topRows(L+1)*svd_inverse_up.U) * svd_inverse_up.S.asDiagonal() * (svd_inverse_up.Vt*update_U.leftCols(L+1)) + Matrix_d::Identity(L+1, L+1)).determinant();
		double d2 = ((update_Vt.topRows(L+1)*svd_inverse_dn.U) * svd_inverse_dn.S.asDiagonal() * (svd_inverse_dn.Vt*update_U.leftCols(L+1)) + Matrix_d::Identity(L+1, L+1)).determinant();
		double s = 1.0;
		if (d1 < 0) {
			s *= -1.0;
			d1 *= -1.0;
		}
		if (d2 < 0) {
			s *= -1.0;
			d2 *= -1.0;
		}
		//double d1 = ( (update_Vt.topRows(L+1)*svdA.Vt.transpose())*svdA.S.array().inverse().matrix().asDiagonal()*(svdA.U.transpose()*update_U.leftCols(L+1))*std::exp(+beta*B*0.5+beta*mu) + Eigen::MatrixXd::Identity(L+1, L+1) ).determinant();
		//double d2 = ( (update_Vt.topRows(L+1)*svdB.Vt.transpose())*svdB.S.array().inverse().matrix().asDiagonal()*(svdB.U.transpose()*update_U.leftCols(L+1))*std::exp(-beta*B*0.5+beta*mu) + Eigen::MatrixXd::Identity(L+1, L+1) ).determinant();
		//std::cerr << L <<  " (" << x << ", " << t << ')' << std::endl;
		return std::pair<double, double>(std::log(d1)+std::log(d2), s);
	}

	void make_tests () {
	}

	bool metropolis (int M = 0) {
		steps++;
		bool ret = false;
		int x = randomPosition(generator);
		int t = randomStep(generator);
		std::pair<double, double> r1 = rank1_probability(x, t);
		ret = -trialDistribution(generator)<r1.first-update_prob;
		if (ret) {
			//std::cerr << "accepted" << std::endl;
			diagonal(t)[x] = -diagonal(t)[x];
			slices[t/mslices] += cache.u_smart*cache.v_smart.transpose();
			update_size++;
			update_prob = r1.first;
			update_sign = r1.second;
			//last_t = t;
		} else {
		}
		return ret;
	}

	double fraction_completed () const {
		return 1.0;
	}

	void update () {
		//std::ofstream out("list_svd.dat", std::ios::app);
		//out << svd.S.array().log().transpose() << ' ' << beta*(-mu-B*0.5) << ' ' << beta*(-mu+B*0.5) << std::endl;
		for (int i=0;i<flips_per_update;i++) {
			acceptance.add(metropolis()?1.0:0.0);
			sign.add(psign*update_sign);
			measured_sign.add(psign*update_sign);
			if (update_size>=max_update_size) {
				plog += update_prob;
				psign *= update_sign;
				make_svd();
				make_svd_inverse();
				reset_updates();
			}
			//make_tests();
		}
		time_shift = randomTime(generator);
		make_slices();
		redo_all();
		//std::cerr << "update finished" << std::endl;
		//std::ofstream out("error.dat");
		//for (int i=0;i<N;i++) {
			//if (error[i].samples()>0) out << i << ' ' << error[i].mean() << std::endl;
		//}
	}

	double get_kinetic_energy (const Matrix_d &M) {
		positionSpace_c = M.cast<Complex>();
		fftw_execute(x2p_col);
		momentumSpace.applyOnTheLeft(energies.asDiagonal());
		fftw_execute(p2x_col);
		return positionSpace_c.real().trace() / V;
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

	void measure () {
		double s = svd_sign();
		rho_up = Matrix_d::Identity(V, V) - svdA.inverse();
		rho_dn = svdB.inverse();
		double K_up = get_kinetic_energy(rho_up);
		double K_dn = get_kinetic_energy(rho_dn);
		double n_up = rho_up.diagonal().array().sum();
		double n_dn = rho_dn.diagonal().array().sum();
		double op = (rho_up.diagonal().array()-rho_dn.diagonal().array()).square().sum();
		double n2 = (rho_up.diagonal().array()*rho_dn.diagonal().array()).sum();
		density.add(s*(n_up+n_dn)/V);
		magnetization.add(s*(n_up-n_dn)/2.0/V);
		//magnetization_slow.add(s*(n_up-n_dn)/2.0/V);
		order_parameter.add(op);
		kinetic.add(s*K_up-s*K_dn);
		interaction.add(s*g*n2);
		//sign.add(svd_sign());
		//- (d1_up*d2_up).sum() - (d1_dn*d2_dn).sum();
		for (int i=0;i<V;i++) {
			d_up[i].add(s*rho_up(i, i));
			d_dn[i].add(s*rho_dn(i, i));
		}
		double d_wave_chi = 0.0;
		Matrix_d F_up = svdA.inverse();
		Matrix_d F_dn = Matrix_d::Identity(V, V) - svdB.inverse();
		const double dtau = beta/slices.size();
		for (const Matrix_d& U : slices) {
			F_up.applyOnTheLeft(U*std::exp(+dtau*B*0.5+dtau*mu));
			F_dn.applyOnTheLeft(U*std::exp(-dtau*B*0.5+dtau*mu));
			d_wave_chi += pair_correlation(F_up, F_dn);
		}
		chi_d.add(s*d_wave_chi*beta/slices.size());
		double af_ =((rho_up.diagonal().array()-rho_dn.diagonal().array())*staggering).sum()/double(V);
		chi_af.add(s*beta*af_*af_);
		for (int k=1;k<=Lx/2;k++) {
			double ssz = 0.0;
			for (int j=0;j<V;j++) {
				int x = j;
				int y = shift_x(j, k);
				ssz += rho_up(x, x)*rho_up(y, y) + rho_dn(x, x)*rho_dn(y, y);
				ssz -= rho_up(x, x)*rho_dn(y, y) + rho_dn(x, x)*rho_up(y, y);
				ssz -= rho_up(x, y)*rho_up(y, x) + rho_dn(x, y)*rho_dn(y, x);
			}
			spincorrelation[k].add(s*0.25*ssz);
			if (isnan(ssz)) {
				//std::cerr << "explain:" << std::endl;
				//std::cerr << "k=" << k << " ssz=" << ssz << std::endl;
				//for (int j=0;j<V;j++) {
					//int x = j;
					//int y = shift_x(j, k);
					//std::cerr << " j=" << j
						//<< " a_j=" << (rho_up(x, x)*rho_up(y, y) + rho_dn(x, x)*rho_dn(y, y))
						//<< " b_j=" << (rho_up(x, x)*rho_dn(y, y) + rho_dn(x, x)*rho_up(y, y))
						//<< " c_j=" << (rho_up(x, y)*rho_up(y, x) + rho_dn(x, y)*rho_dn(y, x)) << std::endl;
				//}
				//throw "";
			}
		}
		if (staggered_field!=0.0) staggered_magnetization.add(s*(rho_up.diagonal().array()*staggering - rho_dn.diagonal().array()*staggering).sum()/V);
	}

	int volume () { return V; }
	int timeSlices () { return N; }

	void output_results () {
		std::ostringstream buf;
		buf << outfn << "stablefast_U" << (g/tx) << "_T" << 1.0/(beta*tx) << '_' << Lx << 'x' << Ly << 'x' << Lz << ".dat";
		outfn = buf.str();
		std::ofstream out(outfn, reset?std::ios::trunc:std::ios::app);
		out << 1.0/(beta*tx) << ' ' << 0.5*(B+g)/tx
			<< ' ' << density.mean() << ' ' << density.variance()
			<< ' ' << magnetization.mean() << ' ' << magnetization.variance()
			//<< ' ' << acceptance.mean() << ' ' << acceptance.variance()
			<< ' ' << kinetic.mean()/tx/V << ' ' << kinetic.variance()/tx/tx/V/V
			<< ' ' << interaction.mean()/tx/V << ' ' << interaction.variance()/tx/tx/V/V;
		out << ' ' << order_parameter.mean() << ' ' << order_parameter.variance();
		out << ' ' << chi_af.mean() << ' ' << chi_af.variance();
		out << ' ' << chi_d.mean() << ' ' << chi_d.variance();
		out << ' ' << measured_sign.mean() << ' ' << measured_sign.variance();
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
	}

	std::string params () {
		std::ostringstream buf;
		buf << "T=" << 1.0/(beta*tx) << "";
		return buf.str();
	}

	~Simulation () {
		fftw_destroy_plan(x2p_vec);
		fftw_destroy_plan(p2x_vec);
		fftw_destroy_plan(x2p_col);
		fftw_destroy_plan(p2x_col);
		fftw_destroy_plan(x2p_row);
		fftw_destroy_plan(p2x_row);
	}
	protected:
};


#endif // SIMULATION_HPP
