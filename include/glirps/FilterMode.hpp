#pragma once

/// @file
/// Filter response selection types.

namespace glirps
{
	/// One response of the shared state variable filter.
	enum class FilterMode
	{
		/// Passes content below the cutoff.
		lowpass,
		/// Passes content around the cutoff.
		bandpass,
		/// Passes content above the cutoff.
		highpass,
		/// Passes everything except content around the cutoff.
		notch,
	};

	/// The responses the shared filter emits, summed.
	/// All four false is a defined state: the filter passes its input through
	/// unfiltered.
	struct FilterModeSet
	{
		/// Whether the lowpass response is summed into the output.
		bool lowpass = true;
		/// Whether the bandpass response is summed into the output.
		bool bandpass = false;
		/// Whether the highpass response is summed into the output.
		bool highpass = false;
		/// Whether the notch response is summed into the output.
		bool notch = false;
	};
}
