/* Copyright (C)
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

#include "new_menu.h"
#include "radio.h"
#include "message.h"
#include "actions.h"
#include "action_dialog.h"
#include "g2panel.h"


int g2panel_menu_is_open = 0;
static int *last_vec = NULL;
static int last_andromeda_type = 0;
static int last_type = 0;
static int action_chosen = NO_ACTION;
static int last_pos = 0;

static GtkWidget *dialog = NULL;
static GtkWidget *last_label = NULL;
static GtkWidget *newAction = NULL;
static GtkWidget *restore = NULL;

static void cleanup() {
  g2panel_menu_is_open = 0;
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
  last_label = NULL;
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static gboolean action_cb(GtkWidget *widget, gpointer data) {
  action_chosen = action_dialog(dialog, last_type, last_vec[last_pos]);
  last_vec[last_pos] = action_chosen;
  //t_print("%s: new action=%d\n", __FUNCTION__, thisAction);
  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[action_chosen].str);
  return TRUE;
}

static gboolean restore_cb(GtkWidget *widget, gpointer data) {
  int *vec;
  switch (last_type) {
  case CONTROLLER_SWITCH:
    vec = g2panel_default_buttons(last_andromeda_type);
    if (vec != NULL) {
      for (int i = 0; i <= 99; i++) { last_vec[i] = vec[i]; }
      g_free(vec);
    }
    break;
  case CONTROLLER_ENCODER:
    vec = g2panel_default_encoders(last_andromeda_type);
    if (vec != NULL) {
      for (int i = 0; i <= 99; i++) { last_vec[i] = vec[i]; }
      g_free(vec);
    }
    break;
  }
  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[last_vec[last_pos]].str);
  return TRUE;
}

void g2panel_menu(GtkWidget *parent) {
  GtkWidget *w;
  GtkWidget *grid;
  dialog = gtk_dialog_new();
  int row = 0;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - G2 panel Switch/Encoder Assignments");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 1, 1);
 
  row++;
  w = gtk_label_new("Last panel element touched:");
  gtk_widget_set_name(w, "medium_button");
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 5, 1);
  last_label = gtk_label_new("  ");
  gtk_widget_set_name(last_label, "medium_button");
  gtk_widget_set_halign(last_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), last_label, 5, row, 3, 1);

  row++;
  w = gtk_label_new("piHPSDR command assigned:");
  gtk_widget_set_name(w, "medium_button");
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 5, 2);

  newAction = gtk_button_new_with_label("    ");
  gtk_widget_set_name(newAction, "medium_button");
  g_signal_connect(newAction, "button-press-event", G_CALLBACK(action_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), newAction, 5, row, 3, 2);

  row++;
  row++;
  restore = gtk_button_new_with_label("Restore factory settings for ENCODERS");
  gtk_widget_set_name(restore, "medium_button");
  g_signal_connect(restore, "button-press-event", G_CALLBACK(restore_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), restore, 0, row, 8, 1);

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  g2panel_menu_is_open = 1;
  gtk_widget_show_all(dialog);
  gtk_widget_hide(newAction);
  gtk_widget_hide(restore);
}

void assign_g2panel_button(int *code) {
  t_print("%s: code=%d\n", *code);
}

void assign_g2panel_encoder(int *code) {
  t_print("%s: code=%d\n", *code);
}

void g2panel_change_button(int type, int *vec, int button) {
  char str[128];
  last_vec = vec;
  last_andromeda_type = type;
  last_pos = button;
  last_type = CONTROLLER_SWITCH;
  snprintf(str, 128, "Button #%d", button);
  gtk_label_set_text(GTK_LABEL(last_label), str);
  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[vec[last_pos]].str);
  gtk_button_set_label(GTK_BUTTON(restore), "Restore factory settings for BUTTONS");
  gtk_widget_show(newAction);
  gtk_widget_show(restore);
}

void g2panel_change_encoder(int type, int *vec, int encoder) {
  char str[128];
  last_vec = vec;
  last_andromeda_type = type;
  last_pos = encoder;
  last_type = CONTROLLER_ENCODER;
  snprintf(str, 128, "Encoder #%d", encoder);
  gtk_label_set_text(GTK_LABEL(last_label), str);
  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[vec[last_pos]].str);
  gtk_button_set_label(GTK_BUTTON(restore), "Restore factory settings for ENCODERS");
  gtk_widget_show(newAction);
  gtk_widget_show(restore);
}
