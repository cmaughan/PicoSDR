namespace MPico
{
    void audio_task();
    void audio_init();
    void audio_set_frequency(uint32_t frequency);
    void audio_add_sample(float sample);
}