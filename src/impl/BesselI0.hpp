#pragma once

#include <cmath>

namespace glirps::impl
{
	// Zeroth-order modified Bessel function of the first kind, evaluated with
	// the polynomial approximations of Abramowitz and Stegun 9.8.1 and 9.8.2.
	// The Kaiser window of the decimator needs this function; the standard
	// library provides no portable implementation (libc++ omits the C++17
	// special math functions), so it is declared here.
	inline double besselI0(double x) noexcept
	{
		const double ax = std::fabs(x);
		if (ax < 3.75)
		{
			const double t = x / 3.75;
			const double t2 = t * t;
			const double inner = 0.2659732 + t2 * (0.0360768 + t2 * 0.0045813);
			const double mid = 1.2067492 + t2 * inner;
			const double outer = 3.0899424 + t2 * mid;
			const double poly = 1.0 + t2 * (3.5156229 + t2 * outer);
			return poly;
		}

		const double t = 3.75 / ax;
		const double y = 0.39894228
			+ t
				* (0.01328592
					+ t
						* (0.00225319
							+ t
								* (-0.00157565
									+ t
										* (0.00916281
											+ t
												* (-0.02057706
													+ t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
		return std::exp(ax) / std::sqrt(ax) * y;
	}
}
