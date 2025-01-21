/* Copyright (C)
* 2025 - Christoph van W"ullen, DL1YCF
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

#ifndef _G2PANEL_MENU_H
#define _G2PANEL_MENU_H

extern void g2panel_menu(GtkWidget *parent);
extern void assign_g2panel_button(int *code);
extern void assign_g2panel_encoder(int *code);
extern void g2panel_change_button(int type, int *vec, int button);
extern void g2panel_change_encoder(int type, int *vec, int encoder);

extern int g2panel_menu_is_open;
#endif
