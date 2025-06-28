/* Copyright (C)
* 2025 - Windows/MinGW port for piHPSDR
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#ifdef _WIN32

#include <gtk/gtk.h>
#include <windows.h>
#include <mmeapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Ensure snprintf is available
#ifndef snprintf
#define snprintf _snprintf
#endif

#include "message.h"
#include "../src/midi_menu.h"
#include "../src/midi.h"
#include "../src/actions.h"

/* Global MIDI variables */
MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
int n_midi_devices = 0;

static HMIDIIN hMidiIn[MAX_MIDI_DEVICES];
static BOOL midi_device_opened[MAX_MIDI_DEVICES];

// MIDI input callback function
void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, 
                        DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    int device_index = (int)dwInstance;
    
    switch (wMsg) {
        case MIM_DATA: {
            // Extract MIDI data
            BYTE status = (BYTE)(dwParam1 & 0xFF);
            BYTE data1 = (BYTE)((dwParam1 >> 8) & 0xFF);
            BYTE data2 = (BYTE)((dwParam1 >> 16) & 0xFF);
            
            // Create MIDI event
            if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80) {
                // Note on/off message
                int channel = status & 0x0F;
                int note = data1;
                int velocity = data2;
                
                // Process the MIDI message
                NewMidiEvent(MIDI_NOTE, channel, note, velocity);
            } else if ((status & 0xF0) == 0xB0) {
                // Control change message
                int channel = status & 0x0F;
                int controller = data1;
                int value = data2;
                
                // Process the MIDI control change
                NewMidiEvent(MIDI_CTRL, channel, controller, value);
            }
            break;
        }
        case MIM_OPEN:
            t_print("MIDI device %d opened\n", device_index);
            break;
        case MIM_CLOSE:
            t_print("MIDI device %d closed\n", device_index);
            break;
        case MIM_ERROR:
            t_print("MIDI error on device %d\n", device_index);
            break;
    }
}

void close_midi_device(int index) {
    t_print("%s index=%d\n", __FUNCTION__, index);
    
    if (index < 0 || index >= MAX_MIDI_DEVICES) {
        return;
    }
    
    if (!midi_device_opened[index]) {
        return;
    }
    
    // Stop and close the MIDI input device
    midiInStop(hMidiIn[index]);
    midiInClose(hMidiIn[index]);
    
    midi_device_opened[index] = FALSE;
    midi_devices[index].active = 0;
}

void register_midi_device(int index) {
    t_print("%s: index=%d\n", __FUNCTION__, index);
    
    if (index < 0 || index >= n_midi_devices) {
        return;
    }
    
    if (midi_device_opened[index]) {
        close_midi_device(index);
    }
    
    // Open the MIDI input device
    MMRESULT result = midiInOpen(&hMidiIn[index], index, (DWORD_PTR)MidiInProc, 
                                index, CALLBACK_FUNCTION);
    
    if (result != MMSYSERR_NOERROR) {
        char error_msg[256];
        midiInGetErrorText(result, error_msg, sizeof(error_msg));
        t_print("Failed to open MIDI device %d: %s\n", index, error_msg);
        return;
    }
    
    // Start recording MIDI input
    result = midiInStart(hMidiIn[index]);
    if (result != MMSYSERR_NOERROR) {
        char error_msg[256];
        midiInGetErrorText(result, error_msg, sizeof(error_msg));
        t_print("Failed to start MIDI device %d: %s\n", index, error_msg);
        midiInClose(hMidiIn[index]);
        return;
    }
    
    midi_device_opened[index] = TRUE;
    midi_devices[index].active = 1;
    t_print("MIDI device %d successfully opened and started\n", index);
}

void midi_save_state() {
    // Save MIDI configuration (implementation can be added later)
}

void midi_restore_state() {
    // Restore MIDI configuration (implementation can be added later)
}

void get_midi_devices() {
    int devices;
    MIDIINCAPS caps;
    
    t_print("%s\n", __FUNCTION__);
    
    // Initialize arrays
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        midi_device_opened[i] = FALSE;
        hMidiIn[i] = NULL;
        midi_devices[i].name = NULL;  // Initialize name pointer
        midi_devices[i].active = 0;
    }
    
    // Get number of MIDI input devices
    devices = midiInGetNumDevs();
    t_print("Found %d MIDI input devices\n", devices);
    
    if (devices > MAX_MIDI_DEVICES) {
        devices = MAX_MIDI_DEVICES;
    }
    
    n_midi_devices = devices;
    
    // Get device capabilities
    for (int i = 0; i < devices; i++) {
        MMRESULT result = midiInGetDevCaps(i, &caps, sizeof(caps));
        if (result == MMSYSERR_NOERROR) {
            // Allocate memory for device name
            midi_devices[i].name = malloc(strlen(caps.szPname) + 1);
            if (midi_devices[i].name) {
                strcpy(midi_devices[i].name, caps.szPname);
            } else {
                midi_devices[i].name = malloc(32);
                if (midi_devices[i].name) {
                    strcpy(midi_devices[i].name, "Unknown Device");
                }
            }
            midi_devices[i].active = 0;
            t_print("MIDI device %d: %s\n", i, midi_devices[i].name ? midi_devices[i].name : "NULL");
        } else {
            // Allocate memory for unknown device name
            midi_devices[i].name = malloc(32);
            if (midi_devices[i].name) {
                snprintf(midi_devices[i].name, 32, "Unknown Device %d", i);
            }
            midi_devices[i].active = 0;
        }
    }
}

void configure_midi_device_windows(int index, int bank) {
    // Configure MIDI device (implementation can be extended)
    if (index >= 0 && index < n_midi_devices) {
        t_print("Configuring MIDI device %d for bank %d\n", index, bank);
    }
}

void configure_midi_device(gboolean state) {
    // Configure MIDI device (standard interface)
    t_print("Configure MIDI device called with state: %s\n", state ? "TRUE" : "FALSE");
    // Implementation can be added later if needed
}

void cleanup_midi_devices() {
    // Free allocated memory for device names
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        if (midi_devices[i].name) {
            free(midi_devices[i].name);
            midi_devices[i].name = NULL;
        }
        midi_devices[i].active = 0;
    }
    n_midi_devices = 0;
}

#endif // _WIN32
