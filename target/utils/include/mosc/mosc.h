namespace MPico
{

enum class ClockOutput
{
    CLOCK_0,
    CLOCK_1,
    CLOCK_2
};

void m_osc_init();
void m_osc_set_frequency(uint64_t frequency, ClockOutput clock);

}