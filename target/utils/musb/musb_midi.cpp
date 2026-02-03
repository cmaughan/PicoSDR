
#include <bsp/board_api.h>
#include <musb/tusb_config.h>
#include <mosc/mosc.h>
#include <tusb.h>

#include <pico/stdlib.h>

#include <pico_zest/time/pico_profiler.h>

using namespace Zest;

namespace MPico
{
void audio_set_frequency(uint32_t frequency);
namespace
{

// Variable that holds the current position in the sequence.
uint32_t note_pos = 0;

// Store example melody as an array of note values
const uint8_t note_sequence[] = {
    74, 78, 81, 86, 90, 93, 98, 102, 57, 61, 66, 69, 73, 78, 81, 85, 88, 92, 97, 100, 97, 92, 88, 85, 81, 78,
    74, 69, 66, 62, 57, 62, 66, 69, 74, 78, 81, 86, 90, 93, 97, 102, 97, 93, 90, 85, 81, 78, 73, 68, 64, 61,
    56, 61, 64, 68, 74, 78, 81, 86, 90, 93, 98, 102
};

std::vector<uint8_t> midi_input_buffer;
void append_midi_from_usb_midi_event(const uint8_t ev[4])
{
    uint8_t cin = ev[0] & 0x0F; // low nibble

    switch (cin)
    {
        // 3-byte messages (note on/off, CC, etc.)
        case 0x8: // Note Off
        case 0x9: // Note On
        case 0xA: // Poly Key Pressure
        case 0xB: // Control Change
        case 0xE: // Pitch Bend
        case 0x4: // SysEx start/continue (3 data bytes)
            midi_input_buffer.push_back(ev[1]);
            midi_input_buffer.push_back(ev[2]);
            midi_input_buffer.push_back(ev[3]);
            break;

        // 2-byte messages
        case 0xC: // Program Change
        case 0xD: // Channel Pressure
            midi_input_buffer.push_back(ev[1]);
            midi_input_buffer.push_back(ev[2]);
            break;

        // SysEx end with 1/2/3 data bytes
        case 0x5: // SysEx end, 1 byte
            midi_input_buffer.push_back(ev[1]);
            break;

        case 0x6: // SysEx end, 2 bytes
            midi_input_buffer.push_back(ev[1]);
            midi_input_buffer.push_back(ev[2]);
            break;

        case 0x7: // SysEx end, 3 bytes
            midi_input_buffer.push_back(ev[1]);
            midi_input_buffer.push_back(ev[2]);
            midi_input_buffer.push_back(ev[3]);
            break;

        // Single-byte messages (e.g. real-time)
        case 0xF:
            midi_input_buffer.push_back(ev[1]);
            break;

        // Other CIN values you don't care about right now
        default:
            // ignore or log
            break;
    }
}

void midi_read_command()
{
    uint8_t packet[4];
    if (tud_midi_packet_read(packet))
    {
        append_midi_from_usb_midi_event(packet);
    }

    // Find our system message
    uint32_t start_index = 0;
    uint32_t end_index = 0;
    for (auto index = 0; index < midi_input_buffer.size(); index++) 
    {
        if (midi_input_buffer[index] == 0xF0)
        {
            start_index = index;
        }

        if (midi_input_buffer[index] == 0xF7)
        {
            end_index = index;
            break;
        }
    }

    // Skip the sys
    start_index += 2;

    // We need at least 5 bytes for a frequency value
    if (end_index > (start_index + 4))
    {
        uint32_t val = 0; 
        for (auto i = start_index; i < end_index; i++)
        {
            val |= ((midi_input_buffer[i] & 0x7F) << (7 * (i - start_index)));
        } 

        // Temporary hack to test received message
        if (val <= 7300000 && val > 7000000)
        {
            m_osc_set_frequency(val, ClockOutput::CLOCK_0);
        }
        midi_input_buffer.clear();
    }

    // sanity; make sure it doesn't grow too large
    if (midi_input_buffer.size() > 30)
    {
        midi_input_buffer.clear();
    }
}

void send_test_notes()
{
    uint8_t const cable_num = 0; // MIDI jack associated with USB endpoint
    uint8_t const channel = 0; // 0 for channel 1
    static uint32_t start_ms = 0;

    // send note periodically
    if (board_millis() - start_ms < 286)
    {
        return; // not enough time
    }
    start_ms += 286;

    // Previous positions in the note sequence.
    int previous = (int)(note_pos - 1);

    // If we currently are at position 0, set the
    // previous position to the last note in the sequence.
    if (previous < 0)
    {
        previous = sizeof(note_sequence) - 1;
    }

    // Send Note On for current position at full velocity (127) on channel 1.
    uint8_t note_on[3] = { 0x90 | channel, note_sequence[note_pos], 127 };
    tud_midi_stream_write(cable_num, note_on, 3);

    // Send Note Off for previous note.
    uint8_t note_off[3] = { 0x80 | channel, note_sequence[previous], 0 };
    tud_midi_stream_write(cable_num, note_off, 3);

    // Increment position
    note_pos++;

    // If we are at the end of the sequence, start over.
    if (note_pos >= sizeof(note_sequence))
    {
        note_pos = 0;
    }

}

} // namespace

void midi_task(void)
{
    PROFILE_SCOPE(midi_task);

    // Always have to drain the midi queue
    while (tud_midi_available())
    {
        midi_read_command();
    }

    // Example send of notes for checking
    send_test_notes();
}

} // namespace MPico
