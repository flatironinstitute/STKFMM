/*
 * LaplaceM2L.cpp
 *
 *  Created on: Oct 12, 2016
 *      Author: wyan
 */

#include "SVD_pvfmm.hpp"

namespace Laplace3D3D {

/*******************************************
 *
 *    Laplace Potential Summation
 *
 *********************************************/
inline double freal(double xi, double r) { return std::erfc(xi * r) / r; }

inline double frealp(double xi, double r) {
    return -(2. * exp(-r * r * (xi * xi)) * xi) / (sqrt(M_PI) * r) - std::erfc(r * xi) / (r * r);
}

// xm: target, xn: source
inline double realSum(const double xi, const EVec3 &xn, const EVec3 &xm) {
    EVec3 rmn = xm - xn;
    double rnorm = rmn.norm();
    if (rnorm < eps) {
        return 0;
    }
    return freal(xi, rnorm);
}

inline double gKernelEwald(const EVec3 &xm, const EVec3 &xn) {
    const double xi = 2; // recommend for box=1 to get machine precision
    EVec3 target = xm;
    EVec3 source = xn;
    target[0] = target[0] - floor(target[0]); // periodic BC
    target[1] = target[1] - floor(target[1]);
    target[2] = target[2] - floor(target[2]);
    source[0] = source[0] - floor(source[0]);
    source[1] = source[1] - floor(source[1]);
    source[2] = source[2] - floor(source[2]);

    // real sum
    int rLim = 4;
    double Kreal = 0;
    for (int i = -rLim; i <= rLim; i++) {
        for (int j = -rLim; j <= rLim; j++) {
            for (int k = -rLim; k <= rLim; k++) {
                Kreal += realSum(xi, target, source - EVec3(i, j, k));
            }
        }
    }

    // wave sum
    int wLim = 4;
    double Kwave = 0;
    EVec3 rmn = target - source;
    const double xi2 = xi * xi;
    const double rmnnorm = rmn.norm();
    for (int i = -wLim; i <= wLim; i++) {
        for (int j = -wLim; j <= wLim; j++) {
            for (int k = -wLim; k <= wLim; k++) {
                if (i == 0 && j == 0 && k == 0) {
                    continue;
                }
                EVec3 kvec = EVec3(i, j, k) * (2 * M_PI);
                double k2 = kvec.dot(kvec);
                Kwave += 4 * M_PI * cos(kvec.dot(rmn)) * exp(-k2 / (4 * xi2)) / k2;
            }
        }
    }

    double Kself = rmnnorm < 1e-10 ? -2 * xi / sqrt(M_PI) : 0;

    return (Kreal + Kwave + Kself - M_PI / xi2) / (4 * M_PI);
}

inline double gKernel(const EVec3 &target, const EVec3 &source) {
    EVec3 rst = target - source;
    double rnorm = rst.norm();
    return rnorm < eps ? 0 : 1 / (4 * M_PI * rnorm);
}

inline double gKernelNF(const EVec3 &target, const EVec3 &source, int N = DIRECTLAYER) {
    double gNF = 0;
    for (int i = -N; i < N + 1; i++) {
        for (int j = -N; j < N + 1; j++) {
            for (int k = -N; k < N + 1; k++) {
                gNF += gKernel(target, source + EVec3(i, j, k));
            }
        }
    }
    return gNF;
}

// Out of Direct Sum Layer, far field part
inline double gKernelFF(const EVec3 &target, const EVec3 &source) {
    double fEwald = gKernelEwald(target, source);
    fEwald -= gKernelNF(target, source);
    return fEwald;
}

/*******************************************
 *
 *    Laplace Grad Summation
 *
 *********************************************/

inline void realGradSum(double xi, const EVec3 &target, const EVec3 &source, EVec3 &v) {
    EVec3 rvec = target - source;
    double rnorm = rvec.norm();
    if (rnorm < eps) {
        v.setZero();
    } else {
        v = (frealp(xi, rnorm) / rnorm) * rvec;
    }
}

inline void gradkernel(const EVec3 &target, const EVec3 &source, EVec3 &answer) {
    // grad of Laplace potential
    EVec3 rst = target - source;
    double rnorm = rst.norm();
    if (rnorm < eps) {
        answer.setZero();
        return;
    }
    double rnorm3 = rnorm * rnorm * rnorm;
    answer = -rst / (rnorm3 * 4 * M_PI);
}

inline void gradEwald(const EVec3 &target_, const EVec3 &source_, EVec3 &answer) {
    // grad of Laplace potential, periodic of -r_k/r^3
    EVec3 target = target_;
    EVec3 source = source_;
    target[0] = target[0] - floor(target[0]); // periodic BC
    target[1] = target[1] - floor(target[1]);
    target[2] = target[2] - floor(target[2]);
    source[0] = source[0] - floor(source[0]);
    source[1] = source[1] - floor(source[1]);
    source[2] = source[2] - floor(source[2]);

    double xi = 0.54;

    // real sum
    int rLim = 10;
    EVec3 Kreal = EVec3::Zero();
    for (int i = -rLim; i < rLim + 1; i++) {
        for (int j = -rLim; j < rLim + 1; j++) {
            for (int k = -rLim; k < rLim + 1; k++) {
                EVec3 v = EVec3::Zero();
                realGradSum(xi, target, source + EVec3(i, j, k), v);
                Kreal += v;
            }
        }
    }

    // wave sum
    int wLim = 10;
    EVec3 rmn = target - source;
    double xi2 = xi * xi;
    EVec3 Kwave(0., 0., 0.);
    for (int i = -wLim; i < wLim + 1; i++) {
        for (int j = -wLim; j < wLim + 1; j++) {
            for (int k = -wLim; k < wLim + 1; k++) {
                if (i == 0 && j == 0 && k == 0)
                    continue;
                EVec3 kvec = EVec3(i, j, k) * (2 * M_PI);
                double k2 = kvec.dot(kvec);
                double knorm = kvec.norm();
                Kwave += -kvec * (sin(kvec.dot(rmn)) * exp(-k2 / (4 * xi2)) / k2);
            }
        }
    }

    answer = (Kreal + Kwave) / (4 * M_PI);
}

inline EVec4 ggradKernel(const EVec3 &target, const EVec3 &source) {
    EVec3 rst = target - source;
    EVec4 pgrad = EVec4::Zero();
    double rnorm = rst.norm();
    if (rnorm < eps) {
        pgrad.setZero();
    } else {
        pgrad[0] = 1 / rnorm;
        double rnorm3 = rnorm * rnorm * rnorm;
        pgrad.block<3, 1>(1, 0) = -rst / rnorm3;
    }
    return pgrad / (4 * M_PI);
}

inline EVec4 ggradKernelEwald(const EVec3 &target, const EVec3 &source) {
    EVec4 pgrad = EVec4::Zero();
    pgrad[0] = gKernelEwald(target, source);
    EVec3 grad;
    gradEwald(target, source, grad);
    pgrad[1] = grad[0];
    pgrad[2] = grad[1];
    pgrad[3] = grad[2];
    return pgrad;
}

inline EVec4 ggradKernelNF(const EVec3 &target, const EVec3 &source, int N = DIRECTLAYER) {
    EVec4 gNF = EVec4::Zero();
    for (int i = -N; i < N + 1; i++) {
        for (int j = -N; j < N + 1; j++) {
            for (int k = -N; k < N + 1; k++) {
                EVec4 gFree = ggradKernel(target, source + EVec3(i, j, k));
                gNF += gFree;
            }
        }
    }
    return gNF;
}

// Out of Direct Sum Layer, far field part
inline EVec4 ggradKernelFF(const EVec3 &target, const EVec3 &source) {
    EVec4 fEwald = ggradKernelEwald(target, source);
    fEwald -= ggradKernelNF(target, source);
    return fEwald;
}

// integrate gKernel(trg-src) in [0,1]^3
double gdv(const EVec3 &src) {
    const int N = 64;
    const double points[64] = {-1.,
                               -0.9987569212189223,
                               -0.9950307753654014,
                               -0.9888308262251285,
                               -0.9801724878485438,
                               -0.969077286229078,
                               -0.9555728057861407,
                               -0.9396926207859084,
                               -0.9214762118704076,
                               -0.9009688679024191,
                               -0.8782215733702285,
                               -0.8532908816321557,
                               -0.8262387743159949,
                               -0.7971325072229225,
                               -0.766044443118978,
                               -0.7330518718298263,
                               -0.6982368180860727,
                               -0.6616858375968594,
                               -0.6234898018587336,
                               -0.58374367223479,
                               -0.5425462638657594,
                               -0.4999999999999999,
                               -0.4562106573531631,
                               -0.4112871031306117,
                               -0.365341024366395,
                               -0.31848665025168443,
                               -0.27084046814300516,
                               -0.22252093395631445,
                               -0.17364817766693041,
                               -0.12434370464748527,
                               -0.07473009358642439,
                               -0.024930691738073035,
                               0.024930691738072913,
                               0.07473009358642427,
                               0.12434370464748516,
                               0.1736481776669303,
                               0.22252093395631434,
                               0.270840468143005,
                               0.3184866502516843,
                               0.3653410243663949,
                               0.4112871031306114,
                               0.4562106573531626,
                               0.5000000000000002,
                               0.5425462638657593,
                               0.5837436722347896,
                               0.6234898018587335,
                               0.6616858375968592,
                               0.6982368180860727,
                               0.7330518718298263,
                               0.7660444431189779,
                               0.7971325072229225,
                               0.8262387743159947,
                               0.8532908816321556,
                               0.8782215733702287,
                               0.900968867902419,
                               0.9214762118704077,
                               0.9396926207859083,
                               0.9555728057861406,
                               0.9690772862290778,
                               0.9801724878485438,
                               0.9888308262251285,
                               0.9950307753654014,
                               0.9987569212189223,
                               1.};

    const double weights[64] = {
        0.000251952632905016, 0.002426825577091332, 0.004985682800997252, 0.007422108895215611, 0.009886791733980155,
        0.01230107564932005,  0.01470112902792591,  0.017053372663619858, 0.019371425756378687, 0.02163508760827156,
        0.023849839358342426, 0.02600138565292559,  0.028091483494470625, 0.03010909164371389,  0.03205406836747507,
        0.0339174677291859,   0.0356981543681018,   0.037388704313402986, 0.03898749674668903,  0.04048832316267236,
        0.041889389342280314, 0.04318553037633184,  0.04437498297779121,  0.045453526843574316, 0.046419569176223564,
        0.047269776669786025, 0.048002824246101534, 0.04861623221720065,  0.04910901048034227,  0.04947951406104648,
        0.04972713211259671,  0.049851044314039916, 0.049851044314039916, 0.04972713211259671,  0.04947951406104648,
        0.04910901048034227,  0.04861623221720065,  0.048002824246101534, 0.047269776669786025, 0.046419569176223564,
        0.045453526843574316, 0.04437498297779121,  0.04318553037633184,  0.041889389342280314, 0.04048832316267236,
        0.03898749674668903,  0.037388704313402986, 0.0356981543681018,   0.0339174677291859,   0.03205406836747507,
        0.03010909164371389,  0.028091483494470625, 0.02600138565292559,  0.023849839358342426, 0.02163508760827156,
        0.019371425756378687, 0.017053372663619858, 0.01470112902792591,  0.01230107564932005,  0.009886791733980155,
        0.007422108895215611, 0.004985682800997252, 0.002426825577091332, 0.000251952632905016};

    double res = 0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            for (int k = 0; k < N; k++) {
                EVec3 trg((points[i] + 1) / 2, (points[j] + 1) / 2, (points[k] + 1) / 2);
                res += gKernel(trg, src) * weights[i] * weights[j] * weights[k] / 8;
            }
        }
    }
    return res;
}

int main(int argc, char **argv) {
    Eigen::initParallel();
    Eigen::setNbThreads(1);
    constexpr int kdim[2] = {1, 1}; // target, source dimension

    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    const int pEquiv = atoi(argv[1]);
    const int pCheck = atoi(argv[1]);

    const double pCenterMEquiv[3] = {-(scaleIn - 1) / 2, -(scaleIn - 1) / 2, -(scaleIn - 1) / 2};
    const double pCenterMCheck[3] = {-(scaleOut - 1) / 2, -(scaleOut - 1) / 2, -(scaleOut - 1) / 2};

    const double pCenterLEquiv[3] = {-(scaleOut - 1) / 2, -(scaleOut - 1) / 2, -(scaleOut - 1) / 2};
    const double pCenterLCheck[3] = {-(scaleIn - 1) / 2, -(scaleIn - 1) / 2, -(scaleIn - 1) / 2};

    auto pointMEquiv = surface(pEquiv, (double *)&(pCenterMEquiv[0]), scaleIn, 0);
    auto pointMCheck = surface(pCheck, (double *)&(pCenterMCheck[0]), scaleOut, 0);

    auto pointLCheck = surface(pCheck, (double *)&(pCenterLCheck[0]), scaleIn, 0);
    auto pointLEquiv = surface(pEquiv, (double *)&(pCenterLEquiv[0]), scaleOut, 0);

    const int equivN = pointMEquiv.size() / 3;
    const int checkN = pointMCheck.size() / 3;
    EMat M2L(kdim[1] * equivN, kdim[1] * equivN); // M2L density
    EMat M2C(kdim[0] * checkN, kdim[1] * equivN); // M2C check surface

    EMat AL(kdim[0] * checkN, kdim[1] * equivN); // L den to L check
    EMat ALpinvU(AL.cols(), AL.rows());
    EMat ALpinvVT(AL.cols(), AL.rows());
#pragma omp parallel for
    for (int k = 0; k < checkN; k++) {
        EVec3 Cpoint(pointLCheck[3 * k], pointLCheck[3 * k + 1], pointLCheck[3 * k + 2]);
        for (int l = 0; l < equivN; l++) {
            const EVec3 Lpoint(pointLEquiv[3 * l], pointLEquiv[3 * l + 1], pointLEquiv[3 * l + 2]);
            AL(k, l) = gKernel(Cpoint, Lpoint);
        }
    }
    pinv(AL, ALpinvU, ALpinvVT);

    EVec qint(checkN);
    for (int l = 0; l < checkN; l++) {
        const EVec3 Lpoint(pointLEquiv[3 * l], pointLEquiv[3 * l + 1], pointLEquiv[3 * l + 2]);
        qint[l] = gdv(Lpoint);
    }

#pragma omp parallel for
    for (int i = 0; i < equivN; i++) {
        const EVec3 Mpoint(pointMEquiv[3 * i], pointMEquiv[3 * i + 1], pointMEquiv[3 * i + 2]);
        const EVec3 Npoint(0.5, 0.5, 0.5); // neutralizing

        EVec f(checkN);
        EVec q(checkN);
        for (int k = 0; k < checkN; k++) {
            EVec3 Cpoint(pointLCheck[3 * k], pointLCheck[3 * k + 1], pointLCheck[3 * k + 2]);
            f[k] = gKernelFF(Cpoint, Mpoint) - gKernelFF(Cpoint, Npoint); // sum the images
        }                                                                 // enforce vol integral = 0
        // solve equivalent strength at 2.95 surface from f(k)
        // q = ALpinvU.transpose() * (ALpinvVT.transpose() * f);
        // double volint = qint.dot(q);
        // printf("%g\n", volint);

        // for (int k = 0; k < checkN; k++) {
        //     f[k] -= volint;
        // }

        M2C.col(i) = f;
        M2L.col(i) = (ALpinvU.transpose() * (ALpinvVT.transpose() * f));
    }
    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

    std::cout << "Precomputing time:" << duration / 1e6 << std::endl;

    saveEMat(M2L, "M2L_laplace_3D3D_p" + std::to_string(pEquiv));
    saveEMat(M2C, "M2C_laplace_3D3D_p" + std::to_string(pEquiv));

    EMat AM(kdim[0] * checkN, kdim[1] * equivN); // M den to M check
    EMat AMpinvU(AM.cols(), AM.rows());
    EMat AMpinvVT(AM.cols(), AM.rows());
#pragma omp parallel for
    for (int k = 0; k < checkN; k++) {
        EVec3 Cpoint(pointMCheck[3 * k], pointMCheck[3 * k + 1], pointMCheck[3 * k + 2]);
        for (int l = 0; l < equivN; l++) {
            const EVec3 Mpoint(pointMEquiv[3 * l], pointMEquiv[3 * l + 1], pointMEquiv[3 * l + 2]);
            AM(k, l) = gKernel(Cpoint, Mpoint);
        }
    }
    pinv(AM, AMpinvU, AMpinvVT);

    // Test
    EVec3 center(0.6, 0.5, 0.5);
    std::vector<EVec3, Eigen::aligned_allocator<EVec3>> chargePoint(2);
    std::vector<double> chargeValue(2);
    chargePoint[0] = center + EVec3(0.1, 0, 0);
    chargeValue[0] = 1;
    chargePoint[1] = center + EVec3(-0.1, 0., 0.);
    chargeValue[1] = -1;

    // solve M
    EVec f(checkN);
    for (int k = 0; k < checkN; k++) {
        double temp = 0;
        EVec3 Cpoint(pointMCheck[3 * k], pointMCheck[3 * k + 1], pointMCheck[3 * k + 2]);
        for (size_t p = 0; p < chargePoint.size(); p++) {
            temp = temp + gKernel(Cpoint, chargePoint[p]) * (chargeValue[p]);
        }
        f[k] = temp;
    }
    EVec Msource = (AMpinvU.transpose() * (AMpinvVT.transpose() * f));
    EVec M2Lsource = M2L * (Msource);

    std::cout << "Msource: " << Msource.transpose() << std::endl;
    std::cout << "M2Lsource: " << M2Lsource.transpose() << std::endl;

    std::cout << "backward error: " << f - AM * Msource << std::endl;

    // check dipole moment
    {
        EVec3 dipole = EVec3::Zero();
        for (int i = 0; i < chargePoint.size(); i++) {
            dipole += chargeValue[i] * chargePoint[i];
        }
        std::cout << "charge dipole " << dipole.transpose() << std::endl;
    }
    {
        EVec3 dipole = EVec3::Zero();
        for (int i = 0; i < equivN; i++) {
            EVec3 Mpoint(pointMEquiv[3 * i], pointMEquiv[3 * i + 1], pointMEquiv[3 * i + 2]);
            dipole += Mpoint * Msource[i];
        }
        std::cout << "Mequiv dipole " << dipole.transpose() << std::endl;
    }

    for (int is = 0; is < 5; is++) {

        EVec3 samplePoint = EVec3::Random() * 0.2 + EVec3(0.5, 0.5, 0.5);

        EVec4 UFFL2T = EVec4::Zero();
        EVec4 UFFS2T = EVec4::Zero();
        EVec4 UFFM2T = EVec4::Zero();

#pragma omp sections
        {
#pragma omp section
            for (int p = 0; p < chargePoint.size(); p++) {
                UFFS2T += ggradKernelFF(samplePoint, chargePoint[p]) * chargeValue[p];
            }

#pragma omp section
            for (int p = 0; p < equivN; p++) {
                EVec3 Lpoint(pointLEquiv[3 * p], pointLEquiv[3 * p + 1], pointLEquiv[3 * p + 2]);
                UFFL2T += ggradKernel(samplePoint, Lpoint) * M2Lsource[p];
            }

#pragma omp section
            for (int p = 0; p < equivN; p++) {
                EVec3 Mpoint(pointMEquiv[3 * p], pointMEquiv[3 * p + 1], pointMEquiv[3 * p + 2]);
                UFFM2T += ggradKernelFF(samplePoint, Mpoint) * Msource[p];
            }
        }
        std::cout << std::scientific << std::setprecision(10);
        std::cout << "-----------------------------------------------" << std::endl;
        std::cout << "samplePoint:" << samplePoint.transpose() << std::endl;
        std::cout << "UFF S2T: " << UFFS2T.transpose() << std::endl;
        std::cout << "UFF M2T: " << UFFM2T.transpose() << std::endl;
        std::cout << "UFF L2T: " << UFFL2T.transpose() << std::endl;
        std::cout << "Error M2T: " << (UFFM2T - UFFS2T).transpose() << std::endl;
        std::cout << "Error L2T: " << (UFFL2T - UFFS2T).transpose() << std::endl;
    }

    return 0;
}

} // namespace Laplace3D3D
