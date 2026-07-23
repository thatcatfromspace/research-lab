#ifndef ANALYZER_ZIPFIAN_GENERATOR_HPP
#define ANALYZER_ZIPFIAN_GENERATOR_HPP

#include <cmath>
#include <random>
#include <vector>
#include <stdexcept>

namespace analyzer {

class ZipfianGenerator {
public:
    ZipfianGenerator(int max_val, double theta = 0.99)
        : max_val_(max_val), theta_(theta), rng_(std::random_device{}()) {
        if (max_val <= 0) throw std::invalid_argument("max_val must be > 0");
        zeta_n_ = zeta(max_val_, theta_);
        alpha_ = 1.0 / (1.0 - theta_);
        eta_ = (1.0 - std::pow(2.0 / max_val_, 1.0 - theta_)) / (1.0 - zeta(2, theta_) / zeta_n_);
    }

    int next() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double u = dist(rng_);
        double uz = u * zeta_n_;
        
        if (uz < 1.0) return 0;
        if (uz < 1.0 + std::pow(0.5, theta_)) return 1;
        
        int ret = static_cast<int>(max_val_ * std::pow(eta_ * u - eta_ + 1.0, alpha_));
        if (ret >= max_val_) ret = max_val_ - 1;
        return ret;
    }

private:
    int max_val_;
    double theta_;
    std::mt19937 rng_;
    double alpha_;
    double eta_;
    double zeta_n_;

    double zeta(int n, double theta) {
        double sum = 0.0;
        for (int i = 1; i <= n; i++) {
            sum += 1.0 / std::pow(i, theta);
        }
        return sum;
    }
};

} // namespace analyzer

#endif // ANALYZER_ZIPFIAN_GENERATOR_HPP
