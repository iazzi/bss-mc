#ifndef HUBBARD_HPP
#define HUBBARD_HPP

#include <Eigen/Dense>
#include <random>

struct HubbardVertex {
	int x;
	double sigma;
	double tau;
	struct Compare {
		bool operator() (const HubbardVertex& a, const HubbardVertex& b) {
			return (a.tau<b.tau) || (a.tau==b.tau && a.x<b.x)
				|| (a.tau==b.tau && a.x==b.x && (std::fabs(a.sigma)<std::fabs(b.sigma)))
				|| (a.tau==b.tau && a.x==b.x && std::fabs(a.sigma)==std::fabs(b.sigma) && a.sigma<b.sigma);
		}
	};
};

class HubbardInteraction {
	std::mt19937_64 &generator;
	Eigen::MatrixXd eigenvectors;
	double U;
	double K;
	double N;
	double a, b;
	std::bernoulli_distribution coin_flip;
	std::uniform_int_distribution<size_t> random_site;
	public:
	typedef HubbardVertex Vertex;
	HubbardInteraction (std::mt19937_64 &g) : generator(g) {}
	void setup (const Eigen::MatrixXd &A, double u, double k);
	Vertex generate ();
	Vertex generate (double tau);
	Vertex generate (double t0, double t1);
	template <typename T>
		void apply_vertex_on_the_left (Vertex v, T &M) {
			M += v.sigma * eigenvectors.row(v.x).transpose() * (eigenvectors.row(v.x) * M);
		}

	template <typename T>
		void apply_vertex_on_the_right (Vertex v, T &M) {
			M += v.sigma * (M * eigenvectors.row(v.x).transpose()) * eigenvectors.row(v.x);
		}

	template <typename T>
		void apply_inverse_on_the_left (Vertex v, T &M) {
			M -= v.sigma/(1.0+v.sigma) * eigenvectors.row(v.x).transpose() * (eigenvectors.row(v.x) * M);
		}

	template <typename T>
		void apply_inverse_on_the_right (Vertex v, T &M) {
			M -= v.sigma/(1.0+v.sigma) * (M * eigenvectors.row(v.x).transpose()) * eigenvectors.row(v.x);
		}
};

#endif // HUBBARD_HPP

