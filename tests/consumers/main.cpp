// A minimal consumer: renders the start of one sound and succeeds when it is
// not silent. Built as C++20, which is all a consumer needs.

#include <glirps/glirps.hpp>
#include <glirps/glirps_handles.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main()
{
	constexpr std::uint32_t sample_rate_hz = 48000;

	const glirps::VoiceProgram voice;
	glirps::SoundProgram program;
	program.voices = &voice;
	program.voice_count = 1;

	const glirps::CreateResult created = glirps::synthCreate(1, 1);
	if (!created)
		return EXIT_FAILURE;
	const glirps::SynthOwner synth(created.synth);
	glirps::synthConfigure(created.synth, sample_rate_hz, glirps::default_oversampling_factor);
	const auto started = glirps::synthStart(created.synth, program, 1);
	if (!started)
		return EXIT_FAILURE;

	glirps::ParameterBlock block;
	if (glirps::synthEvaluateTick(
			created.synth, started.instance, glirps::shims::Span<glirps::ParameterBlock>(&block, 1))
		!= 1)
		return EXIT_FAILURE;
	glirps::synthApplyBlock(created.synth, block);

	std::array<float, std::size_t{8} * glirps::sub_block_frame_count> samples{};
	glirps::synthRender(created.synth, glirps::shims::toSpan(samples));
	glirps::synthRelease(created.synth, started.instance);

	if (std::ranges::all_of(samples, [](float sample) { return sample == 0.0f; }))
		return EXIT_FAILURE;
	std::puts("GLIRPS consumer: rendered audio");
	return EXIT_SUCCESS;
}
