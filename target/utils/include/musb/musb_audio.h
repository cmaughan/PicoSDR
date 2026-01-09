#include <musb/tusb_config.h>
namespace MPico
{
    void audio_task();
    void audio_init();
    void audio_set_frequency(uint32_t frequency);
    void audio_add_sample(float sample);
}

#define AUDIO_SAMPLE_RATE CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE