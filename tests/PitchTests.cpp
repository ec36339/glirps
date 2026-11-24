#include "glirps/frequency.hpp"

#include <gtest/gtest.h>

#include <cmath>

// Note-plus-cents to frequency conversion: pitch is stored as note plus
// cents, not as frequency in hertz.
TEST(PitchTests, FrequencyHertz_OneOctaveUp_DoublesFrequency)
{
	// ARRANGE
	const glirps::Pitch base{.semitone = 48, .cents = 0};
	const glirps::Pitch up{.semitone = 60, .cents = 0};

	// ACT
	const float base_hz = glirps::frequencyHertz(base);
	const float up_hz = glirps::frequencyHertz(up);

	// ASSERT
	EXPECT_NEAR(up_hz / base_hz, 2.0f, 0.001f);
}

TEST(PitchTests, FrequencyHertz_PlusMinus1200Cents_MoveExactlyOneOctave)
{
	// ARRANGE
	const glirps::Pitch base{.semitone = 48, .cents = 0};
	const glirps::Pitch plus{.semitone = 48, .cents = 1200};
	const glirps::Pitch minus{.semitone = 48, .cents = -1200};

	// ACT
	const float base_hz = glirps::frequencyHertz(base);
	const float plus_hz = glirps::frequencyHertz(plus);
	const float minus_hz = glirps::frequencyHertz(minus);

	// ASSERT
	EXPECT_NEAR(plus_hz / base_hz, 2.0f, 0.001f);
	EXPECT_NEAR(minus_hz / base_hz, 0.5f, 0.001f);
}

TEST(PitchTests, FrequencyHertz_A4_Is440Hz)
{
	// ARRANGE
	const glirps::Pitch a4{.semitone = 57, .cents = 0};

	// ACT & ASSERT
	EXPECT_FLOAT_EQ(glirps::frequencyHertz(a4), 440.0f);
}
