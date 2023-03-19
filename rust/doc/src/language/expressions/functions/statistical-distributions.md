# Statistical Distribution Functions

PSPP can calculate several functions of standard statistical
distributions.  These functions are named systematically based on the
function and the distribution.  The table below describes the
statistical distribution functions in general:

* `PDF.DIST(X[, PARAM...])`  
  Probability density function for `DIST`.  The domain of `X` depends on
  `DIST`.  For continuous distributions, the result is the density of
  the probability function at X, and the range is nonnegative real
  numbers.  For discrete distributions, the result is the probability
  of `X`.

* `CDF.DIST(X[, PARAM...])`  
  Cumulative distribution function for `DIST`, that is, the probability
  that a random variate drawn from the distribution is less than `X`.
  The domain of `X` depends `DIST`.  The result is a probability.

* `SIG.DIST(X[, PARAM...)`  
  Tail probability function for `DIST`, that is, the probability that a
  random variate drawn from the distribution is greater than `X`.  The
  domain of `X` depends `DIST`.  The result is a probability.  Only a few
  distributions include an `SIG` function.

* `IDF.DIST(P[, PARAM...])`  
  Inverse distribution function for `DIST`, the value of `X` for which
  the CDF would yield P.  The value of P is a probability.  The range
  depends on `DIST` and is identical to the domain for the
  corresponding CDF.

* `RV.DIST([PARAM...])`  
  Random variate function for `DIST`.  The range depends on the
  distribution.

* `NPDF.DIST(X[, PARAM...])`  
  Noncentral probability density function.  The result is the density
  of the given noncentral distribution at `X`.  The domain of `X` depends
  on `DIST`.  The range is nonnegative real numbers.  Only a few
  distributions include an `NPDF` function.

* `NCDF.DIST(X[, PARAM...])`  
  Noncentral cumulative distribution function for `DIST`, that is, the
  probability that a random variate drawn from the given noncentral
  distribution is less than `X`.  The domain of `X` depends `DIST`.  The
  result is a probability.  Only a few distributions include an NCDF
  function.

## Continuous Distributions

The following continuous distributions are available:

* `PDF.BETA(X)`  
  `CDF.BETA(X, A, B)`  
  `IDF.BETA(P, A, B)`  
  `RV.BETA(A, B)`  
  `NPDF.BETA(X, A, B, ꟛ)`  
  `NCDF.BETA(X, A, B, ꟛ)`  
  Beta distribution with shape parameters `A` and `B`.  The noncentral
  distribution takes an additional parameter ꟛ.  Constraints: `A > 0, B > 0, ꟛ >= 0, 0 <= X <= 1, 0 <= P <= 1`.

* `PDF.BVNOR(X0, X1, ρ)`  
  `CDF.BVNOR(X0, X1, ρ)`  
  Bivariate normal distribution of two standard normal variables with
  correlation coefficient ρ.  Two variates X0 and X1 must be
  provided.  Constraints: 0 <= ρ <= 1, 0 <= P <= 1.

* `PDF.CAUCHY(X, A, B)`  
  `CDF.CAUCHY(X, A, B)`  
  `IDF.CAUCHY(P, A, B)`  
  `RV.CAUCHY(A, B)`  
  Cauchy distribution with location parameter `A` and scale parameter
  `B`.  Constraints: B > 0, 0 < P < 1.

* `CDF.CHISQ(X, DF)`  
  `SIG.CHISQ(X, DF)`  
  `IDF.CHISQ(P, DF)`  
  `RV.CHISQ(DF)`  
  `NCDF.CHISQ(X, DF, ꟛ)`  
  Chi-squared distribution with DF degrees of freedom.  The
  noncentral distribution takes an additional parameter ꟛ.
  Constraints: DF > 0, ꟛ > 0, X >= 0, 0 <= P < 1.

* `PDF.EXP(X, A)`  
  `CDF.EXP(X, A)`  
  `IDF.EXP(P, A)`  
  `RV.EXP(A)`  
  Exponential distribution with scale parameter `A`.  The inverse of `A`
  represents the rate of decay.  Constraints: A > 0, X >= 0, 0 <= P <
  1.

* `PDF.XPOWER(X, A, B)`  
  `RV.XPOWER(A, B)`  
  Exponential power distribution with positive scale parameter `A` and
  nonnegative power parameter `B`.  Constraints: A > 0, B >= 0, X >= 0,
  0 <= P <= 1.  This distribution is a PSPP extension.

* `PDF.F(X, DF1, DF2)`  
  `CDF.F(X, DF1, DF2)`  
  `SIG.F(X, DF1, DF2)`  
  `IDF.F(P, DF1, DF2)`  
  `RV.F(DF1, DF2)`  
  F-distribution of two chi-squared deviates with DF1 and DF2 degrees
  of freedom.  The noncentral distribution takes an additional
  parameter ꟛ.  Constraints: DF1 > 0, DF2 > 0, ꟛ >= 0, X >=
  0, 0 <= P < 1.

* `PDF.GAMMA(X, A, B)`  
  `CDF.GAMMA(X, A, B)`  
  `IDF.GAMMA(P, A, B)`  
  `RV.GAMMA(A, B)`  
  Gamma distribution with shape parameter `A` and scale parameter `B`.
  Constraints: A > 0, B > 0, X >= 0, 0 <= P < 1.

* `PDF.LANDAU(X)`  
  `RV.LANDAU()`  
  Landau distribution.

* `PDF.LAPLACE(X, A, B)`  
  `CDF.LAPLACE(X, A, B)`  
  `IDF.LAPLACE(P, A, B)`  
  `RV.LAPLACE(A, B)`  
  Laplace distribution with location parameter `A` and scale parameter
  `B`.  Constraints: B > 0, 0 < P < 1.

* `RV.LEVY(C, ɑ)`  
  Levy symmetric alpha-stable distribution with scale C and exponent
  ɑ.  Constraints: 0 < ɑ <= 2.

* `RV.LVSKEW(C, ɑ, β)`  
  Levy skew alpha-stable distribution with scale C, exponent ɑ, and
  skewness parameter β.  Constraints: 0 < ɑ <= 2, -1 <= β <= 1.

* `PDF.LOGISTIC(X, A, B)`  
  `CDF.LOGISTIC(X, A, B)`  
  `IDF.LOGISTIC(P, A, B)`  
  `RV.LOGISTIC(A, B)`  
  Logistic distribution with location parameter `A` and scale parameter
  `B`.  Constraints: B > 0, 0 < P < 1.

* `PDF.LNORMAL(X, A, B)`  
  `CDF.LNORMAL(X, A, B)`  
  `IDF.LNORMAL(P, A, B)`  
  `RV.LNORMAL(A, B)`  
  Lognormal distribution with parameters `A` and `B`.  Constraints: A >
  0, B > 0, X >= 0, 0 <= P < 1.

* `PDF.NORMAL(X, μ, σ)`  
  `CDF.NORMAL(X, μ, σ)`  
  `IDF.NORMAL(P, μ, σ)`  
  `RV.NORMAL(μ, σ)`  
  Normal distribution with mean μ and standard deviation σ.
  Constraints: B > 0, 0 < P < 1.  Three additional functions are
  available as shorthand:

  * `CDFNORM(X)`  
    Equivalent to `CDF.NORMAL(X, 0, 1)`.

  * `PROBIT(P)`  
    Equivalent to `IDF.NORMAL(P, 0, 1)`.

  * `NORMAL(σ)`  
    Equivalent to `RV.NORMAL(0, σ)`.

* `PDF.NTAIL(X, A, σ)`  
  `RV.NTAIL(A, σ)`  
  Normal tail distribution with lower limit `A` and standard deviation
  `σ`.  This distribution is a PSPP extension.  Constraints: A >
  0, X > A, 0 < P < 1.

* `PDF.PARETO(X, A, B)`  
  `CDF.PARETO(X, A, B)`  
  `IDF.PARETO(P, A, B)`  
  `RV.PARETO(A, B)`  
  Pareto distribution with threshold parameter `A` and shape parameter
  `B`.  Constraints: A > 0, B > 0, X >= A, 0 <= P < 1.

* `PDF.RAYLEIGH(X, σ)`  
  `CDF.RAYLEIGH(X, σ)`  
  `IDF.RAYLEIGH(P, σ)`  
  `RV.RAYLEIGH(σ)`  
  Rayleigh distribution with scale parameter σ.  This
  distribution is a PSPP extension.  Constraints: σ > 0, X > 0.

* `PDF.RTAIL(X, A, σ)`  
  `RV.RTAIL(A, σ)`  
  Rayleigh tail distribution with lower limit `A` and scale parameter
  `σ`.  This distribution is a PSPP extension.  Constraints: A > 0,
  σ > 0, X > A.

* `PDF.T(X, DF)`  
  `CDF.T(X, DF)`  
  `IDF.T(P, DF)`  
  `RV.T(DF)`  
  T-distribution with DF degrees of freedom.  The noncentral
  distribution takes an additional parameter ꟛ.  Constraints: DF > 0,
  0 < P < 1.

* `PDF.T1G(X, A, B)`  
  `CDF.T1G(X, A, B)`  
  `IDF.T1G(P, A, B)`  
  Type-1 Gumbel distribution with parameters `A` and `B`.  This
  distribution is a PSPP extension.  Constraints: 0 < P < 1.

* `PDF.T2G(X, A, B)`  
  `CDF.T2G(X, A, B)`  
  `IDF.T2G(P, A, B)`  
  Type-2 Gumbel distribution with parameters `A` and `B`.  This
  distribution is a PSPP extension.  Constraints: X > 0, 0 < P < 1.

* `PDF.UNIFORM(X, A, B)`  
  `CDF.UNIFORM(X, A, B)`  
  `IDF.UNIFORM(P, A, B)`  
  `RV.UNIFORM(A, B)`  
  Uniform distribution with parameters `A` and `B`.  Constraints: A <= X
  <= B, 0 <= P <= 1.  An additional function is available as
  shorthand:

  - `UNIFORM(B)`  
    Equivalent to `RV.UNIFORM(0, B)`.

* `PDF.WEIBULL(X, A, B)`  
  `CDF.WEIBULL(X, A, B)`  
  `IDF.WEIBULL(P, A, B)`  
  `RV.WEIBULL(A, B)`  
  Weibull distribution with parameters `A` and `B`.  Constraints: A > 0,
  B > 0, X >= 0, 0 <= P < 1.

## Discrete Distributions

The following discrete distributions are available:

* `PDF.BERNOULLI(X)`  
  `CDF.BERNOULLI(X, P)`  
  `RV.BERNOULLI(P)`  
   Bernoulli distribution with probability of success P.  Constraints:
   X = 0 or 1, 0 <= P <= 1.

* `PDF.BINOM(X, N, P)`  
  `CDF.BINOM(X, N, P)`  
  `RV.BINOM(N, P)`  
   Binomial distribution with N trials and probability of success P.
   Constraints: integer N > 0, 0 <= P <= 1, integer X <= N.

* `PDF.GEOM(X, N, P)`  
  `CDF.GEOM(X, N, P)`  
  `RV.GEOM(N, P)`  
  Geometric distribution with probability of success P.  Constraints:
  0 <= P <= 1, integer X > 0.

* `PDF.HYPER(X, A, B, C)`  
  `CDF.HYPER(X, A, B, C)`  
  `RV.HYPER(A, B, C)`  
  Hypergeometric distribution when `B` objects out of `A` are drawn and `C`
  of the available objects are distinctive.  Constraints: integer A >
  0, integer B <= A, integer C <= A, integer X >= 0.

* `PDF.LOG(X, P)`  
  `RV.LOG(P)`  
  Logarithmic distribution with probability parameter P.
  Constraints: 0 <= P < 1, X >= 1.

* `PDF.NEGBIN(X, N, P)`  
  `CDF.NEGBIN(X, N, P)`  
  `RV.NEGBIN(N, P)`  
  Negative binomial distribution with number of successes parameter N
  and probability of success parameter P.  Constraints: integer N >=
  0, 0 < P <= 1, integer X >= 1.

* `PDF.POISSON(X, μ)`  
  `CDF.POISSON(X, μ)`  
  `RV.POISSON(μ)`  
  Poisson distribution with mean μ.  Constraints: μ > 0, integer X >= 0.
