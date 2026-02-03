#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tusb.h>

#include <bsp/board_api.h>

#include <pico/stdlib.h>
#include <pico_zest/time/pico_profiler.h>

#include <zest/logger/logger.h>

#include <mled/mled.h>

#include <musb/musb_audio.h>

using namespace Zest;

// Audio controls
// Current states
bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // +1 for master channel 0
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // +1 for master channel 0
uint32_t sampFreq;
uint8_t clkValid;

// Range states
audio20_control_range_2_n_t(1) volumeRng[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // Volume range state
audio20_control_range_4_n_t(1) sampleFreqRng; // Sample frequency range state

const uint32_t buffer_time = AUDIO_SAMPLE_RATE / 1000; // 1ms buffer
const uint32_t buffer_samples = buffer_time * CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX;

const uint32_t num_buffer_pages = 10;
uint32_t write_buffer_page = 0;
uint32_t read_buffer_page = 0;
uint32_t write_buffer_sample = 0;
uint32_t read_buffer_offset = 0;
uint32_t filled_pages = 0;
std::array<std::array<int16_t, buffer_samples>, num_buffer_pages> i2s_buffer;
uint32_t bufferSample = 0;

uint16_t adcVal;

class SineOsc
{
public:
    static constexpr std::size_t TABLE_SIZE = 8096;

    explicit SineOsc(double sampleRate)
        : sampleRate_(sampleRate)
    {
        // Build wavetable
        for (std::size_t i = 0; i < TABLE_SIZE; ++i)
        {
            table_[i] = std::sin(2.0 * M_PI * i / double(TABLE_SIZE));
        }
        setFrequency(440.0);
    }

    void setFrequency(double frequency)
    {
        phaseInc_ = frequency * TABLE_SIZE / sampleRate_;
    }

    // Generate next sample in range [-1.0, 1.0]
    float sample()
    {
        std::size_t idx = static_cast<std::size_t>(phase_);
        float value = table_[idx];

        phase_ += phaseInc_;
        if (phase_ >= TABLE_SIZE)
            phase_ -= TABLE_SIZE;

        return value;
    }

private:
    double sampleRate_;
    double phase_ = 0.0;
    double phaseInc_ = 0.0;

    float table_[TABLE_SIZE];
};

SineOsc sineOsc = SineOsc(AUDIO_SAMPLE_RATE);

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff)
{
    PROFILE_SCOPE(tud_audio_set_req_ep_cb);
    TU_LOG1("tud_audio_set_req_ep_cb\r\n");
    (void)rhport;
    (void)pBuff;

    // We do not support any set range requests here, only current value requests
    TU_VERIFY(p_request->bRequest == AUDIO20_CS_REQ_CUR);

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t ep = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)ep;

    return false; // Yet not implemented
}

// Invoked when audio class specific set request received for an interface
bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff)
{
    PROFILE_SCOPE(tud_audio_set_req_itf_cb);
    TU_LOG1("tud_audio_set_req_itf_cb\r\n");
    (void)rhport;
    (void)pBuff;

    // We do not support any set range requests here, only current value requests
    TU_VERIFY(p_request->bRequest == AUDIO20_CS_REQ_CUR);

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t itf = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)itf;

    return false; // Yet not implemented
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff)
{
    (void)rhport;

    TU_LOG1("tud_audio_set_req_entity_cb\r\n");

    PROFILE_SCOPE(tud_audio_set_req_entity_cb);

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t itf = TU_U16_LOW(p_request->wIndex);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

    (void)itf;

    // We do not support any set range requests here, only current value requests
    TU_VERIFY(p_request->bRequest == AUDIO20_CS_REQ_CUR);

    // If request is for our feature unit
    if (entityID == 2)
    {
        switch (ctrlSel)
        {
        case AUDIO20_FU_CTRL_MUTE:
            // Request uses format layout 1
            TU_VERIFY(p_request->wLength == sizeof(audio20_control_cur_1_t));

            mute[channelNum] = ((audio20_control_cur_1_t*)pBuff)->bCur;

            TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);
            return true;

        case AUDIO20_FU_CTRL_VOLUME:
            // Request uses format layout 2
            TU_VERIFY(p_request->wLength == sizeof(audio20_control_cur_2_t));

            volume[channelNum] = (uint16_t)((audio20_control_cur_2_t*)pBuff)->bCur;

            TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", volume[channelNum], channelNum);
            return true;

            // Unknown/Unsupported control
        default:
            TU_BREAKPOINT();
            return false;
        }
    }
    return false; // Yet not implemented
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
    (void)rhport;

    PROFILE_SCOPE(tud_audio_get_req_ep_cb);
    TU_LOG1("tud_audio_get_req_ep_cb\r\n");

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t ep = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)ep;

    //	return tud_control_xfer(rhport, p_request, &tmp, 1);

    return false; // Yet not implemented
}

// Invoked when audio class specific get request received for an interface
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
    (void)rhport;

    PROFILE_SCOPE(tud_audio_get_req_itf_cb);
    TU_LOG1("tud_audio_get_req_itf_cb\r\n");

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t itf = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)itf;

    return false; // Yet not implemented
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
    (void)rhport;

    PROFILE_SCOPE(tud_audio_get_req_entity_cb);
    TU_LOG1("tud_audio_get_req_entity_cb\r\n");

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    // uint8_t itf = TU_U16_LOW(p_request->wIndex); 			// Since we have only one audio function implemented, we do not need the itf value
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

    // Input terminal (Microphone input)
    if (entityID == 1)
    {
        switch (ctrlSel)
        {
        case AUDIO20_TE_CTRL_CONNECTOR:
        {
            // The terminal connector control only has a get request with only the CUR attribute.
            audio20_desc_channel_cluster_t ret;

            // Those are dummy values for now
            ret.bNrChannels = 1;
            ret.bmChannelConfig = (audio20_channel_config_t)0;
            ret.iChannelNames = 0;

            TU_LOG1("    Get terminal connector\r\n");

            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*)&ret, sizeof(ret));
        }
        break;

            // Unknown/Unsupported control selector
        default:
            TU_BREAKPOINT();
            return false;
        }
    }

    // Feature unit
    if (entityID == 2)
    {
        switch (ctrlSel)
        {
        case AUDIO20_FU_CTRL_MUTE:
            // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
            // There does not exist a range parameter block for mute
            TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
            return tud_control_xfer(rhport, p_request, &mute[channelNum], 1);

        case AUDIO20_FU_CTRL_VOLUME:
            switch (p_request->bRequest)
            {
            case AUDIO20_CS_REQ_CUR:
                TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
                return tud_control_xfer(rhport, p_request, &volume[channelNum], sizeof(volume[channelNum]));

            case AUDIO20_CS_REQ_RANGE:
                TU_LOG2("    Get Volume range of channel: %u\r\n", channelNum);

                // Copy values - only for testing - better is version below
                audio20_control_range_2_n_t(1) ret;

                ret.wNumSubRanges = 1;
                ret.subrange[0].bMin = -90; // -90 dB
                ret.subrange[0].bMax = 90; // +90 dB
                ret.subrange[0].bRes = 1; // 1 dB steps

                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*)&ret, sizeof(ret));

                // Unknown/Unsupported control
            default:
                TU_BREAKPOINT();
                return false;
            }
            break;

            // Unknown/Unsupported control
        default:
            TU_BREAKPOINT();
            return false;
        }
    }

    // Clock Source unit
    if (entityID == 4)
    {
        switch (ctrlSel)
        {
        case AUDIO20_CS_CTRL_SAM_FREQ:
            // channelNum is always zero in this case
            switch (p_request->bRequest)
            {
            case AUDIO20_CS_REQ_CUR:
                TU_LOG1("    Get Sample Freq.\r\n");
                // Buffered control transfer is needed for IN flow control to work
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &sampFreq, sizeof(sampFreq));

            case AUDIO20_CS_REQ_RANGE:
                TU_LOG1("    Get Sample Freq. range\r\n");
                return tud_control_xfer(rhport, p_request, &sampleFreqRng, sizeof(sampleFreqRng));

                // Unknown/Unsupported control
            default:
            {
                TU_LOG1("    Unknown/Unsupported control\r\n");
                TU_BREAKPOINT();
                return false;
            }
            break;
            }
            break;

        case AUDIO20_CS_CTRL_CLK_VALID:
            // Only cur attribute exists for this request
            TU_LOG2("    Get Sample Freq. valid\r\n");
            return tud_control_xfer(rhport, p_request, &clkValid, sizeof(clkValid));

        // Unknown/Unsupported control
        default:
            TU_LOG1("    Unknown/Unsupported control\r\n");
            TU_BREAKPOINT();
            return false;
        }
    }

    TU_LOG1("  Unsupported entity: %d\r\n", entityID);
    return false; // Yet not implemented
}

namespace MPico
{

void audio_init()
{
    LOG(DBG, "audio_init");

    // Init values
    sampFreq = AUDIO_SAMPLE_RATE;
    clkValid = 1;

    sampleFreqRng.wNumSubRanges = 1;
    sampleFreqRng.subrange[0].bMin = AUDIO_SAMPLE_RATE;
    sampleFreqRng.subrange[0].bMax = AUDIO_SAMPLE_RATE;
    sampleFreqRng.subrange[0].bRes = 0;
}

void audio_set_frequency(uint32_t frequency)
{
    LOG(DBG, "audio_set_frequency: " << frequency);
    sineOsc.setFrequency(float(frequency));
}

void audio_add_sample(float sample)
{
    if (filled_pages >= num_buffer_pages)
    {
        return; // buffer full
    }

    i2s_buffer[write_buffer_page][write_buffer_sample++] = int16_t(sample * 32767.0f);
    if (write_buffer_sample >= buffer_samples)
    {
        filled_pages++;
        write_buffer_page = (write_buffer_page + 1) % num_buffer_pages;
        write_buffer_sample = 0;
    }
}

// We assume that the audio data is read from an I2S buffer.
// In a real application, this would be replaced with actual I2S receive callback.
void audio_task(void)
{
    PROFILE_SCOPE(audio_task);

    // TODO: Figure out the timing for this
    // The profiler will show us how close we are to max, as long as we can figure
    // out what that is.  On the PC, it's just the requested frame size and rate.
    // but here we fill up the fifo.
    // PROFILE_REGION(audio_task);
    // Profiler::SetRegionLimit();

    static uint32_t start_ms = 0;
    uint32_t curr_ms = board_millis();
    if (curr_ms < (start_ms + 1))
    {
        return; // not enough time
    }
    start_ms = curr_ms;

    /*
    for (uint32_t sample = 0; sample < buffer_samples; sample++)
    {
        float samp = sineOsc.sample();
        i2s_buffer[current_buffer_page][sample] = int16_t(samp * 32767.0f);
    }
        */

    if (filled_pages == 0)
    {
        return; // no full buffers to send
    }

    const uint32_t samples_available = buffer_samples - read_buffer_offset;
    if (samples_available == 0)
    {
        read_buffer_offset = 0;
        read_buffer_page = (read_buffer_page + 1) % num_buffer_pages;
        filled_pages--;
        return;
    }

    const uint32_t bytes_to_write = samples_available * sizeof(int16_t);
    const uint32_t written = tud_audio_write(
        (const void*)&i2s_buffer[read_buffer_page][read_buffer_offset],
        uint16_t(bytes_to_write));
    const uint32_t written_samples = written / sizeof(int16_t);

    if (written_samples == 0)
    {
        return;
    }

    read_buffer_offset += written_samples;
    if (read_buffer_offset >= buffer_samples)
    {
        read_buffer_offset = 0;
        read_buffer_page = (read_buffer_page + 1) % num_buffer_pages;
        filled_pages--;
    }
}

} // namespace MPico
