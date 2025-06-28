/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2025 - Christoph van Wüllen, DL1YCF
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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <arpa/inet.h>

#include "actions.h"
#include "agc.h"
#include "appearance.h"
#include "band.h"
#include "client_server.h"
#include "discovered.h"
#include "gpio.h"
#include "message.h"
#include "mode.h"
#include "radio.h"
#ifdef USBOZY
  #include "ozyio.h"
#endif
#include "receiver.h"
#include "rx_panadapter.h"
#include "transmitter.h"
#include "vfo.h"

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
panadapter_configure_event_cb (GtkWidget         *widget,
                               GdkEventConfigure *event,
                               gpointer           data) {
  RECEIVER *rx = (RECEIVER *)data;
  int mywidth = gtk_widget_get_allocated_width (widget);
  int myheight = gtk_widget_get_allocated_height (widget);

  if (rx->panadapter_surface) {
    cairo_surface_destroy (rx->panadapter_surface);
  }

  rx->panadapter_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                           CAIRO_CONTENT_COLOR,
                           mywidth, myheight);
  cairo_t *cr = cairo_create(rx->panadapter_surface);
  cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  cairo_paint(cr);
  cairo_destroy(cr);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
panadapter_draw_cb (GtkWidget *widget,
                    cairo_t   *cr,
                    gpointer   data) {
  RECEIVER *rx = (RECEIVER *)data;

  if (rx->panadapter_surface) {
    cairo_set_source_surface (cr, rx->panadapter_surface, 0.0, 0.0);
    cairo_paint (cr);
  }

  return FALSE;
}

static gboolean panadapter_button_press_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  return rx_button_press_event(widget, event, data);
}

static gboolean panadapter_button_release_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  return rx_button_release_event(widget, event, data);
}

static gboolean panadapter_motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  return rx_motion_notify_event(widget, event, data);
}

// cppcheck-suppress constParameterCallback
static gboolean panadapter_scroll_event_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  return rx_scroll_event(widget, event, data);
}

void rx_panadapter_update(RECEIVER *rx) {
  float *samples;
  cairo_text_extents_t extents;
  long long f;
  long long divisor;
  double soffset;
  gboolean active = (active_receiver == rx);
  int mywidth = gtk_widget_get_allocated_width (rx->panadapter);
  int myheight = gtk_widget_get_allocated_height (rx->panadapter);
  samples = rx->pixel_samples;
  cairo_t *cr;
  cr = cairo_create (rx->panadapter_surface);
  cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  cairo_rectangle(cr, 0, 0, mywidth, myheight);
  cairo_fill(cr);
  double HzPerPixel = rx->hz_per_pixel;  // need this many times
  int mode = vfo[rx->id].mode;
  long long frequency = vfo[rx->id].frequency;
  int vfoband = vfo[rx->id].band;
  long long offset;
  //
  // soffset contains all corrections for attenuation and preamps
  // Perhaps some adjustment is necessary for those old radios which have
  // switchable preamps.
  //
  const BAND *band = band_get_band(vfoband);
  int calib = rx_gain_calibration - band->gain;
  soffset = (double) calib + (double)adc[rx->adc].attenuation - adc[rx->adc].gain;

  //
  // offset is used to calculate the filter edges. They move  with the RIT value
  //

  if (vfo[rx->id].ctun) {
    offset = vfo[rx->id].offset;
  } else {
    offset = vfo[rx->id].rit_enabled ? vfo[rx->id].rit : 0;
  }

  if (filter_board == ALEX && rx->adc == 0) {
    soffset += (double)(10 * rx->alex_attenuation - 20 * rx->preamp);
  }

  if (filter_board == CHARLY25 && rx->adc == 0) {
    soffset += (double)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
  }

  // In diversity mode, the RX2 frequency tracks the RX1 frequency
  if (diversity_enabled && rx->id == 1) {
    frequency = vfo[0].frequency;
    vfoband = vfo[0].band;
    mode = vfo[0].mode;
  }

  long long half = (long long)rx->sample_rate / 2LL;
  double vfofreq = ((double) rx->pixels * 0.5) - (double)rx->pan;

  //
  //
  // The CW frequency is the VFO frequency and the center of the spectrum
  // then is at the VFO frequency plus or minus the sidetone frequency. However we
  // will keep the center of the PANADAPTER at the VFO frequency and shift the
  // pixels of the spectrum.
  //
  if (mode == modeCWU) {
    frequency -= cw_keyer_sidetone_frequency;
    vfofreq += (double) cw_keyer_sidetone_frequency / HzPerPixel;
  } else if (mode == modeCWL) {
    frequency += cw_keyer_sidetone_frequency;
    vfofreq -= (double) cw_keyer_sidetone_frequency / HzPerPixel;
  }

  long long min_display = frequency - half + (long long)((double)rx->pan * HzPerPixel);
  long long max_display = min_display + (long long)((double)rx->width * HzPerPixel);

  if (vfoband == band60) {
    for (int i = 0; i < channel_entries; i++) {
      long long low_freq = band_channels_60m[i].frequency - (band_channels_60m[i].width / (long long)2);
      long long hi_freq = band_channels_60m[i].frequency + (band_channels_60m[i].width / (long long)2);
      double x1 = (double) (low_freq - min_display) / HzPerPixel;
      double x2 = (double) (hi_freq - min_display) / HzPerPixel;
      cairo_set_source_rgba(cr, COLOUR_PAN_60M);
      cairo_rectangle(cr, x1, 0.0, x2 - x1, myheight);
      cairo_fill(cr);
    }
  }

  //
  // Filter edges.
  //
  cairo_set_source_rgba (cr, COLOUR_PAN_FILTER);
  double filter_left = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_low + offset) / HzPerPixel);
  double filter_right = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_high + offset) / HzPerPixel);
  cairo_rectangle(cr, filter_left, 0.0, filter_right - filter_left, myheight);
  cairo_fill(cr);

  // plot the levels
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
  } else {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
  }

  double dbm_per_line = (double)myheight / ((double)rx->panadapter_high - (double)rx->panadapter_low);
  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
  char v[32];

  for (int i = rx->panadapter_high; i >= rx->panadapter_low; i--) {
    int mod = abs(i) % rx->panadapter_step;

    if (mod == 0) {
      double y = (double)(rx->panadapter_high - i) * dbm_per_line;
      cairo_move_to(cr, 0.0, y);
      cairo_line_to(cr, mywidth, y);
      snprintf(v, sizeof(v), "%d dBm", i);
      cairo_move_to(cr, 1, y);
      cairo_show_text(cr, v);
    }
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);
  //
  // plot frequency markers
  // calculate a divisor such that we have about 65
  // pixels distance between frequency markers,
  // and then round upwards to the  next 1/2/5 seris
  //
  divisor = (rx->sample_rate * 65) / rx->pixels;

  if (divisor > 500000LL) { divisor = 1000000LL; }
  else if (divisor > 200000LL) { divisor = 500000LL; }
  else if (divisor > 100000LL) { divisor = 200000LL; }
  else if (divisor >  50000LL) { divisor = 100000LL; }
  else if (divisor >  20000LL) { divisor =  50000LL; }
  else if (divisor >  10000LL) { divisor =  20000LL; }
  else if (divisor >   5000LL) { divisor =  10000LL; }
  else if (divisor >   2000LL) { divisor =   5000LL; }
  else if (divisor >   1000LL) { divisor =   2000LL; }
  else { divisor =   1000LL; }

  //
  // Calculate the actual distance of frequency markers
  // (in pixels)
  //
  int marker_distance = (rx->pixels * divisor) / rx->sample_rate;
  f = ((min_display / divisor) * divisor) + divisor;
  cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  //
  // If space is available, increase font size of freq. labels a bit
  //
  int marker_extra = (marker_distance > 100) ? 2 : 0;
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2 + marker_extra);

  while (f < max_display) {
    double x = (double)(f - min_display) / HzPerPixel;
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, myheight);

    //
    // For frequency marker lines very close to the left or right
    // edge, do not print a frequency since this probably won't fit
    // on the screen
    //
    if ((f >= min_display + divisor / 2) && (f <= max_display - divisor / 2)) {
      //
      // For frequencies larger than 10 GHz, we cannot
      // display all digits here so we give three dots
      // and three "MHz" digits
      //
      if (f > 10000000000LL && marker_distance < 80) {
        snprintf(v, sizeof(v), "...%03lld.%03lld", (f / 1000000) % 1000, (f % 1000000) / 1000);
      } else {
        snprintf(v, sizeof(v), "%0lld.%03lld", f / 1000000, (f % 1000000) / 1000);
      }

      // center text at "x" position
      cairo_text_extents(cr, v, &extents);
      cairo_move_to(cr, x - (extents.width / 2.0), 10 + marker_extra);
      cairo_show_text(cr, v);
    }

    f += divisor;
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);

  if (vfoband != band60) {
    // band edges
    if (band->frequencyMin != 0LL) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_line_width(cr, PAN_LINE_THICK);

      if ((min_display < band->frequencyMin) && (max_display > band->frequencyMin)) {
        double x = (double)(band->frequencyMin - min_display) / HzPerPixel;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, myheight);
        cairo_set_line_width(cr, PAN_LINE_EXTRA);
        cairo_stroke(cr);
      }

      if ((min_display < band->frequencyMax) && (max_display > band->frequencyMax)) {
        double x = (double) (band->frequencyMax - min_display) / HzPerPixel;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, myheight);
        cairo_set_line_width(cr, PAN_LINE_EXTRA);
        cairo_stroke(cr);
      }
    }
  }

  if (remoteclient.running) {
    char text[64];
    cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgba(cr, COLOUR_SHADE);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE4);
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&remoteclient.address)->sin_addr), text, 64);
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, ((double)mywidth / 2.0) - (extents.width / 2.0), (double)myheight / 2.0);
    cairo_show_text(cr, text);
  }

  // agc
  if (rx->agc != AGC_OFF) {
    cairo_set_line_width(cr, PAN_LINE_THICK);
    double knee_y = rx->agc_thresh + soffset;
    knee_y = floor((rx->panadapter_high - knee_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));
    double hang_y = rx->agc_hang + soffset;
    hang_y = floor((rx->panadapter_high - hang_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));

    if (rx->agc != AGC_MEDIUM && rx->agc != AGC_FAST) {
      if (active) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      } else {
        cairo_set_source_rgba(cr, COLOUR_ATTN_WEAK);
      }

      cairo_move_to(cr, 40.0, hang_y - 8.0);
      cairo_rectangle(cr, 40, hang_y - 8.0, 8.0, 8.0);
      cairo_fill(cr);
      cairo_move_to(cr, 40.0, hang_y);
      cairo_line_to(cr, (double)mywidth - 40.0, hang_y);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_stroke(cr);
      cairo_move_to(cr, 48.0, hang_y);
      cairo_show_text(cr, "-H");
    }

    if (active) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    }

    cairo_move_to(cr, 40.0, knee_y - 8.0);
    cairo_rectangle(cr, 40, knee_y - 8.0, 8.0, 8.0);
    cairo_fill(cr);
    cairo_move_to(cr, 40.0, knee_y);
    cairo_line_to(cr, (double)mywidth - 40.0, knee_y);
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_stroke(cr);
    cairo_move_to(cr, 48.0, knee_y);
    cairo_show_text(cr, "-G");
  }

  // cursor
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
  } else {
    cairo_set_source_rgba(cr, COLOUR_ALARM_WEAK);
  }

  cairo_move_to(cr, vfofreq + (offset / HzPerPixel), 0.0);
  cairo_line_to(cr, vfofreq + (offset / HzPerPixel), myheight);
  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);
  // signal
  double s1;
  int pan = rx->pan;

  if (radio_is_remote) {
    //
    // A client will only hold as many samples as there are pixels
    //
    pan = 0;
  }

  samples[pan] = -200.0;
  samples[mywidth - 1 + pan] = -200.0;
  //
  // most HPSDR only have attenuation (no gain), while HermesLite-II and SOAPY use gain (no attenuation)
  //
  s1 = (double)samples[pan] + soffset;
  s1 = floor((rx->panadapter_high - s1)
             * (double) myheight
             / (rx->panadapter_high - rx->panadapter_low));
  cairo_move_to(cr, 0.0, s1);

  for (int i = 1; i < mywidth; i++) {
    double s2;
    s2 = (double)samples[i + pan] + soffset;
    s2 = floor((rx->panadapter_high - s2)
               * (double) myheight
               / (rx->panadapter_high - rx->panadapter_low));
    cairo_line_to(cr, i, s2);
  }

  cairo_pattern_t *gradient;
  gradient = NULL;

  if (rx->display_gradient) {
    gradient = cairo_pattern_create_linear(0.0, myheight, 0.0, 0.0);
    // calculate where S9 is
    double S9 = -73;

    if (vfo[rx->id].frequency > 30000000LL) {
      S9 = -93;
    }

    S9 = floor((rx->panadapter_high - S9)
               * (double) myheight
               / (rx->panadapter_high - rx->panadapter_low));
    S9 = 1.0 - (S9 / (double)myheight);

    if (active) {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,         COLOUR_GRAD1);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,      COLOUR_GRAD2);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, COLOUR_GRAD3);
      cairo_pattern_add_color_stop_rgba(gradient, S9,          COLOUR_GRAD4);
    } else {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,         COLOUR_GRAD1_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,      COLOUR_GRAD2_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, COLOUR_GRAD3_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9,          COLOUR_GRAD4_WEAK);
    }

    cairo_set_source(cr, gradient);
  } else {
    //
    // Different shades of white
    //
    if (active) {
      if (!rx->display_filled) {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL3);
      } else {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL2);
      }
    } else {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILL1);
    }
  }

  if (rx->display_filled) {
    cairo_close_path (cr);
    cairo_fill_preserve (cr);
    cairo_set_line_width(cr, PAN_LINE_THIN);
  } else {
    //
    // if not filling, use thicker line
    //
    cairo_set_line_width(cr, PAN_LINE_THICK);
  }

  cairo_stroke(cr);

  if (gradient) {
    cairo_pattern_destroy(gradient);
  }

  if (rx->panadapter_peaks_on != 0) {
    int num_peaks = rx->panadapter_num_peaks;
    gboolean peaks_in_passband = SET(rx->panadapter_peaks_in_passband_filled);
    gboolean hide_noise = SET(rx->panadapter_hide_noise_filled);
    double noise_percentile = (double)rx->panadapter_ignore_noise_percentile;
    int ignore_range_divider = rx->panadapter_ignore_range_divider;
    int ignore_range = (mywidth + ignore_range_divider - 1) / ignore_range_divider; // Round up
    double peaks[num_peaks];
    int peak_positions[num_peaks];

    for (int a = 0; a < num_peaks; a++) {
      peaks[a] = -200;
      peak_positions[a] = 0;
    }

    // Calculate the noise level if needed
    double noise_level = 0.0;

    if (hide_noise) {
      // Dynamically allocate a copy of samples for sorting
      double *sorted_samples = malloc(mywidth * sizeof(double));

      if (sorted_samples != NULL) {
        for (int i = 0; i < mywidth; i++) {
          sorted_samples[i] = (double)samples[i + pan] + soffset;
        }

        qsort(sorted_samples, mywidth, sizeof(double), compare_doubles);
        int index = (int)((noise_percentile / 100.0) * mywidth);
        noise_level = sorted_samples[index] + 3.0;
        free(sorted_samples); // Free memory after use
      }
    }

    // Detect peaks
    double filter_left_bound = peaks_in_passband ? filter_left : 0;
    double filter_right_bound = peaks_in_passband ? filter_right : mywidth;

    for (int i = 1; i < mywidth - 1; i++) {
      if (i >= filter_left_bound && i <= filter_right_bound) {
        double s = (double)samples[i + pan] + soffset;

        // Check if the point is a peak
        if ((!hide_noise || s >= noise_level) && s > samples[i - 1 + pan] && s > samples[i + 1 + pan]) {
          int replace_index = -1;
          int start_range = i - ignore_range;
          int end_range = i + ignore_range;

          // Check if the peak is within the ignore range of any existing peak
          for (int j = 0; j < num_peaks; j++) {
            if (peak_positions[j] >= start_range && peak_positions[j] <= end_range) {
              if (s > peaks[j]) {
                replace_index = j;
                break;
              } else {
                replace_index = -2;
                break;
              }
            }
          }

          // Replace the existing peak if a higher peak is found within the ignore range
          if (replace_index >= 0) {
            peaks[replace_index] = s;
            peak_positions[replace_index] = i;
          }
          // Add the peak if no peaks are found within the ignore range
          else if (replace_index == -1) {
            // Find the index of the lowest peak
            int lowest_peak_index = 0;

            for (int j = 1; j < num_peaks; j++) {
              if (peaks[j] < peaks[lowest_peak_index]) {
                lowest_peak_index = j;
              }
            }

            // Replace the lowest peak if the current peak is higher
            if (s > peaks[lowest_peak_index]) {
              peaks[lowest_peak_index] = s;
              peak_positions[lowest_peak_index] = i;
            }
          }
        }
      }
    }

    // Sort peaks in descending order
    for (int i = 0; i < num_peaks - 1; i++) {
      for (int j = i + 1; j < num_peaks; j++) {
        if (peaks[i] < peaks[j]) {
          double temp_peak = peaks[i];
          peaks[i] = peaks[j];
          peaks[j] = temp_peak;
          int temp_pos = peak_positions[i];
          peak_positions[i] = peak_positions[j];
          peak_positions[j] = temp_pos;
        }
      }
    }

    // Draw peak values on the chart
    cairo_set_source_rgba(cr, COLOUR_PAN_TEXT); // Set text color
    cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
    double previous_text_positions[num_peaks][2]; // Store previous text positions (x, y)

    for (int j = 0; j < num_peaks; j++) {
      previous_text_positions[j][0] = -1; // Initialise x positions
      previous_text_positions[j][1] = -1; // Initialise y positions
    }

    for (int j = 0; j < num_peaks; j++) {
      if (peak_positions[j] > 0) {
        char peak_label[32];
        snprintf(peak_label, sizeof(peak_label), "%.1f", peaks[j]);
        cairo_text_extents(cr, peak_label, &extents);
        // Calculate initial text position: slightly above the peak
        double text_x = peak_positions[j];
        double text_y = floor((rx->panadapter_high - peaks[j])
                              * (double)myheight
                              / (rx->panadapter_high - rx->panadapter_low)) - 5;

        // Ensure text stays within the drawing area
        if (text_y < extents.height) {
          text_y = extents.height; // Push text down to fit inside the top boundary
        }

        // Adjust position to avoid overlap with previous labels
        for (int k = 0; k < j; k++) {
          double prev_x = previous_text_positions[k][0];
          double prev_y = previous_text_positions[k][1];

          if (prev_x >= 0 && prev_y >= 0) {
            double distance_x = fabs(text_x - prev_x);
            double distance_y = fabs(text_y - prev_y);

            if (distance_y < extents.height && distance_x < extents.width) {
              // Try moving vertically first
              if (text_y + extents.height < myheight) {
                text_y += extents.height + 5; // Move below
              } else if (text_y - extents.height > 0) {
                text_y -= extents.height + 5; // Move above
              } else {
                // Move horizontally if no vertical space is available
                if (text_x + extents.width < mywidth) {
                  text_x += extents.width + 5; // Move right
                } else if (text_x - extents.width > 0) {
                  text_x -= extents.width + 5; // Move left
                }
              }
            }
          }
        }

        // Draw text
        cairo_move_to(cr, text_x - (extents.width / 2.0), text_y);
        cairo_show_text(cr, peak_label);
        // Store current text position for overlap checks
        previous_text_positions[j][0] = text_x;
        previous_text_positions[j][1] = text_y;
      }
    }
  }

  if (rx->id == 0) {
    display_panadapter_messages(cr, mywidth, rx->fps);
  }

  //
  // For horizontal stacking, draw a vertical separator,
  // at the right edge of RX1, and at the left
  // edge of RX2.
  //
  if (rx_stack_horizontal && receivers > 1) {
    if (rx->id == 0) {
      cairo_move_to(cr, mywidth - 1, 0);
      cairo_line_to(cr, mywidth - 1, myheight);
    } else {
      cairo_move_to(cr, 0, 0);
      cairo_line_to(cr, 0, myheight);
    }

    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }

  cairo_destroy (cr);
  gtk_widget_queue_draw (rx->panadapter);
}

void rx_panadapter_init(RECEIVER *rx, int width, int height) {
  rx->panadapter_surface = NULL;
  rx->panadapter = gtk_drawing_area_new ();
  gtk_widget_set_size_request (rx->panadapter, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (rx->panadapter, "draw", G_CALLBACK (panadapter_draw_cb), rx);
  g_signal_connect (rx->panadapter, "configure-event", G_CALLBACK (panadapter_configure_event_cb), rx);
  /* Event signals */
  g_signal_connect (rx->panadapter, "motion-notify-event", G_CALLBACK (panadapter_motion_notify_event_cb), rx);
  g_signal_connect (rx->panadapter, "button-press-event", G_CALLBACK (panadapter_button_press_event_cb), rx);
  g_signal_connect (rx->panadapter, "button-release-event", G_CALLBACK (panadapter_button_release_event_cb), rx);
  //
  // Note the scroll event is generated from  to both RX1/RX2 AND the vfo panel and will scroll the active receiver only
  //
  g_signal_connect(rx->panadapter, "scroll_event", G_CALLBACK(panadapter_scroll_event_cb), NULL);
  /* Ask to receive events the drawing area doesn't normally
   * subscribe to. In particular, we need to ask for the
   * button press and motion notify events that want to handle.
   */
  gtk_widget_set_events (rx->panadapter, gtk_widget_get_events (rx->panadapter)
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON_RELEASE_MASK
                         | GDK_BUTTON1_MOTION_MASK
                         | GDK_SCROLL_MASK
                         | GDK_POINTER_MOTION_MASK
                         | GDK_POINTER_MOTION_HINT_MASK);
}

void display_panadapter_messages(cairo_t *cr, int width, unsigned int fps) {
  char text[64];

  if (display_warnings || remoteclient.running) {
    //
    // Sequence errors
    // ADC overloads
    // TX FIFO under- and overruns
    // high SWR warning
    //
    // Are shown on display for 2 seconds
    //
    // If we are the server and there is a client, we must
    // do the display otherwise the indicators will not be
    // cleared after two seconds (remoteclient.running
    // will be FALSE if we are the client)
    //
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    if (sequence_errors != 0) {
      static unsigned int sequence_error_count = 0;
      cairo_move_to(cr, 100.0, 50.0);
      cairo_show_text(cr, "Sequence Error");
      sequence_error_count++;

      if (sequence_error_count == 2 * fps) {
        sequence_errors = 0;
        sequence_error_count = 0;
      }
    }

    if (adc0_overload || adc1_overload) {
      static unsigned int adc_error_count = 0;
      cairo_move_to(cr, 100.0, 70.0);

      if (adc0_overload && !adc1_overload) {
        cairo_show_text(cr, "ADC0 overload");
      }

      if (adc1_overload && !adc0_overload) {
        cairo_show_text(cr, "ADC1 overload");
      }

      if (adc0_overload && adc1_overload) {
        cairo_show_text(cr, "ADC0+1 overload");
      }

      adc_error_count++;

      if (adc_error_count > 2 * fps) {
        adc_error_count = 0;
        adc0_overload = 0;
        adc1_overload = 0;
#ifdef USBOZY
        mercury_overload[0] = 0;
        mercury_overload[1] = 0;
#endif
      }
    }

    if (high_swr_seen) {
      static unsigned int swr_protection_count = 0;
      cairo_move_to(cr, 100.0, 90.0);
      snprintf(text, sizeof(text), "! High SWR");
      cairo_show_text(cr, text);
      swr_protection_count++;

      if (swr_protection_count >= 3 * fps) {
        high_swr_seen = 0;
        swr_protection_count = 0;
      }
    }

    static unsigned int tx_fifo_count = 0;

    if (tx_fifo_underrun) {
      cairo_move_to(cr, 100.0, 110.0);
      cairo_show_text(cr, "TX Underrun");
      tx_fifo_count++;
    }

    if (tx_fifo_overrun) {
      cairo_move_to(cr, 100.0, 130.0);
      cairo_show_text(cr, "TX Overrun");
      tx_fifo_count++;
    }

    if (tx_fifo_count >= 2 * fps) {
      tx_fifo_underrun = 0;
      tx_fifo_overrun = 0;
      tx_fifo_count = 0;
    }
  }

  if (TxInhibit) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_move_to(cr, 100.0, 30.0);
    cairo_show_text(cr, "TX Inhibit");
  }

  if (display_pacurr && radio_is_transmitting() && !TxInhibit) {
    double v;  // value
    int flag;  // 0: dont, 1: do
    static unsigned int count = 0;
    //
    // Display a maximum value twice per second
    // to avoid flicker
    //
    static double max1 = 0.0;
    static double max2 = 0.0;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

    //
    // Supply voltage or PA temperature
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // (3.26*(ExPwr/4096.0) - 0.5) /0.01
      v = 0.0795898 * exciter_power - 50.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, sizeof(text), "%0.0f°C", max1);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
    case NEW_DEVICE_SATURN:
      // 5 (ADC0_avg / 4095 )* VDiv, VDiv = (22.0 + 1.0) / 1.1
      v = 0.02553 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, sizeof(text), "%0.1fV", max1);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 100.0, 30.0);
      cairo_show_text(cr, text);
    }

    //
    // PA current
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // 1270 ((3.26f * (ADC0 / 4096)) / 50) / 0.04
      v = 0.505396 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.0fmA", max2);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 360, Sens = 120
      v = 0.0101750 * ADC1 - 3.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.1fA", max2);
      flag = 1;
      break;

    case NEW_DEVICE_SATURN:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 0, Sens = 66.23
      v = 0.0184358 * ADC1;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.1fA", max2);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 160.0, 30.0);
      cairo_show_text(cr, text);
    }

    if (++count >= fps / 2) { count = 0; }
  }

  if (capture_state == CAP_RECORDING || capture_state == CAP_REPLAY || capture_state == CAP_AVAIL) {
    static unsigned int cap_count = 0;
    double cx = (double) width - 100.0;
    double cy = 30.0;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx, cy +  5.0);
    cairo_line_to(cr, cx + 90.0, cy +  5.0);
    cairo_line_to(cr, cx + 90.0, cy + 20.0);
    cairo_line_to(cr, cx, cy + 20.0);
    cairo_line_to(cr, cx, cy +  5.0);

    if (capture_state == CAP_REPLAY) {
      cairo_move_to(cr, cx + (90.0 * capture_record_pointer) / capture_max, cy +  5.0);
      cairo_line_to(cr, cx + (90.0 * capture_record_pointer) / capture_max, cy + 20.0);
    }

    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy);

    switch (capture_state) {
    case CAP_RECORDING:
      cairo_show_text(cr, "Recording");
      cairo_rectangle(cr, cx, cy + 5.0, (90.0 * capture_record_pointer) / capture_max, 15.0);
      cairo_fill(cr);
      break;

    case CAP_REPLAY:
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_show_text(cr, "Replay");
      cairo_rectangle(cr, cx + 1.0, cy + 6.0, (90.0 * capture_replay_pointer) / capture_max - 1.0, 13.0);
      cairo_fill(cr);
      break;

    case CAP_AVAIL:
      cairo_show_text(cr, "Recorded");
      cairo_rectangle(cr, cx, cy + 5.0, (90.0 * capture_record_pointer) / capture_max, 15.0);
      cairo_fill(cr);
      cap_count++;

      if (cap_count > 30 * fps) {
        capture_state = CAP_GOTOSLEEP;
        schedule_action(CAPTURE, ACTION_PRESSED, 0);
        cap_count = 0;
      }

      break;
    }
  }
}
