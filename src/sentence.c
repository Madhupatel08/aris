/* Functions for handling the sentence structure.

   Copyright (C) 2012, 2013 Ian Dunn.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sentence.h"
#include "app.h"
#include "sen-parent.h"
#include "aris-proof.h"
#include "list.h"
#include "sen-data.h"
#include "callbacks.h"
#include "rules-table.h"
#include "process.h"
#include "goal.h"
#include "vec.h"
#include "conf-file.h"

//#define LETTERS

static char * sen_values[6] = {"media-playback-stop",
			       "help-about",
			       "window-close",
			       "process-stop",
			       "tools-check-spelling",
			       "list-add"};

// GTextCharPredicate for determining the location of the comment.
static gboolean
comment_predicate (gunichar ch, gpointer user_data)
{
  if (ch == SEN_COMMENT_CHAR)
    return TRUE;
  else
    return FALSE;
}


/* Initializes a sentence
 *  input:
 *    sd  - sentence data to initialize from.
 *    sp  - the new parent of this sentence.
 *    fcs - the item that this sentence will come after.
 *  output:
 *    The newly initialized sentence.
 */
sentence *
sentence_init (sen_data * sd, sen_parent * sp, item_t * fcs)
{
  // Initialize the new sentence.
  sentence * sen = NULL;
  sen = (sentence *) calloc (1, sizeof (sentence));
  CHECK_ALLOC (sen, NULL);

  int ln = 0, ret, depth, i = 0;

  // Only proof containers need to worry about line numbers.
  if (sp->type == SEN_PARENT_TYPE_PROOF)
    ln = (fcs) ? sentence_get_line_no (fcs->value) + 1 : 1;

  // Copy the data elements over.
  sen_data_copy (sd, SD(sen));
  free (SD(sen)->indices);
  SD(sen)->line_num = 0;

  depth = SD(sen)->depth;

  // Initialize the GUI components.
  sentence_gui_init (sen);
  sen->parent = sp;
  sen->value_type = VALUE_TYPE_BLANK;
  sen->selected = 0;
  sen->font_resizing = 0;

  // Set the indices.

  SD(sen)->indices = (int *) calloc (depth + 1, sizeof (int));
  CHECK_ALLOC (SD(sen)->indices, NULL);

  if (!SEN_PREM(sen))
    {
      sentence * fcs_sen;
      int index_copy_end;

      fcs_sen = fcs->value;

      index_copy_end = (SEN_DEPTH(fcs_sen) < depth)
	? SEN_DEPTH(fcs_sen) : depth;

      for (i = 0; i < index_copy_end; i++)
	sentence_set_index (sen,i,SEN_IND(fcs_sen,i));

      if (sd->subproof)
	sentence_set_index (sen,i++,ln);
    }

  sentence_set_index (sen,i,-1);

  // Set the data components.

  ret = sentence_update_line_no (sen, ln);
  if (ret == -1)
    return NULL;

  sentence_set_rule (sen, sd->rule);

  if (sd->text)
    {
      ret = sentence_paste_text (sen);
      if (ret == -1 || ret == -2)
	return NULL;
    }
  else
    {
      SD(sen)->text = strdup ("");
      CHECK_ALLOC (SD(sen)->text, NULL);
    }

  sen->references = init_list ();
  if (!sen->references)
    return NULL;

  ret = sentence_update_refs (sen);
  if (ret == -1)
    return NULL;

  sen->reference = 0;
  sentence_set_font (sen, sen->parent->font);
  sentence_set_bg (sen, BG_COLOR_CONC);

  sentence_connect_signals (sen);

  return sen;
}

/* Initializes the gui elements of a sentence.
 * input:
 *   sen - the sentence to initialize the gui elements of.
 * output:
 *   none.
 */
void
sentence_gui_init (sentence * sen)
{
  // Initialize the GUI components.
  sen->panel = gtk_grid_new ();

  sen->line_no = gtk_label_new (NULL);
  gtk_label_set_justify (GTK_LABEL (sen->line_no), GTK_JUSTIFY_FILL);
  gtk_label_set_width_chars (GTK_LABEL (sen->line_no), 3);

  sen->eventbox = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER (sen->eventbox), sen->line_no);
  gtk_event_box_set_above_child (GTK_EVENT_BOX (sen->eventbox), TRUE);

  GtkWidget * widget;
  if (SEN_DEPTH(sen) > 0)
    {
      widget = gtk_label_new (NULL);
      g_object_set (G_OBJECT (widget), "width-chars", 4 * SEN_DEPTH(sen),
		    NULL);
    }

  sen->entry = gtk_text_view_new ();
  gtk_widget_set_hexpand (sen->entry, TRUE);
  gtk_widget_set_halign (sen->entry, GTK_ALIGN_FILL);

  sen->value = gtk_image_new_from_icon_name (sen_values[0], GTK_ICON_SIZE_MENU);

  int left = 0;

  gtk_grid_attach (GTK_GRID (sen->panel), sen->eventbox, left++, 0, 1, 1);
  
  if (SEN_DEPTH(sen) > 0)
    {
      gtk_grid_attach (GTK_GRID (sen->panel), widget, left++, 0, 1, 1);
    }

  gtk_grid_attach (GTK_GRID (sen->panel), sen->entry, left++, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (sen->panel), sen->value, left++, 0, 1, 1);

  sen->rule_box = gtk_label_new (NULL);
  gtk_label_set_justify (GTK_LABEL (sen->rule_box), GTK_JUSTIFY_FILL);
  gtk_label_set_width_chars (GTK_LABEL (sen->rule_box), 2);

  gtk_grid_attach (GTK_GRID (sen->panel), sen->rule_box, left++, 0, 1, 1);

  sen->mark = NULL;

  GtkTextTag * tag;
  GtkTextTagTable * table;

  table =
    gtk_text_buffer_get_tag_table (gtk_text_view_get_buffer
				   (GTK_TEXT_VIEW (sen->entry)));

  tag = gtk_text_tag_new ("hilight");
  g_object_set (G_OBJECT (tag),
		"background-rgba", the_app->bg_colors[BG_COLOR_GOOD],
		NULL);

  gtk_text_tag_table_add (table, tag);

  tag = gtk_text_tag_new ("negative");
  g_object_set (G_OBJECT (tag),
		"background-rgba", the_app->bg_colors[BG_COLOR_BAD],
		NULL);

  gtk_text_tag_table_add (table, tag);
}

/* Destroys a sentence.
 *  input:
 *    sen - the sentence to destroy.
 *  output:
 *    none.
 */
void
sentence_destroy (sentence * sen)
{
  if (sen->references)
    destroy_list (sen->references);
  sen->references = NULL;

  sen->parent = NULL;

  gtk_widget_destroy (sen->panel);
  sen_data_destroy (SD(sen));
}

/* Copies the elements of a sentence into a sen_data structure.
 *  input:
 *    sen - the sentence to copy.
 *  output:
 *    the sen_data structure.
 */
sen_data *
sentence_copy_to_data (sentence * sen)
{
  sen_data * sd;
  int i;

  sd = (sen_data *) calloc (1, sizeof (sen_data));
  CHECK_ALLOC (sd, NULL);
  i = sen_data_copy (SD(sen), sd);
  if (i == -1)
    return NULL;

  return sd;
}

/* Sets the line number of a sentence.
 *  input:
 *    sen - The sentence to set the line number.
 *    new_line_no - The new line number.
 *  output:
 *    -1 on error, -2 if the line isn't meant to be set, 0 on success.
 */
int
sentence_set_line_no (sentence * sen, int new_line_no)
{
  int cur_line = sentence_get_line_no (sen);

  if (new_line_no < 1)
    {
      if (cur_line == -1)
	return -2;

      SD(sen)->line_num = -1;
      gtk_label_set_text (GTK_LABEL (sen->line_no), NULL);
      return 0;
    }

  char * new_label;
  double label_len = log10 ((double) new_line_no);
  int sp_chk = 0;
  SD(sen)->line_num = new_line_no;

  //The length of any number in base 10 will be log10(n) + 1
  new_label = (char *) calloc ((int)label_len + 2, sizeof (char));
  CHECK_ALLOC (new_label, -1);

  sp_chk = sprintf (new_label, "%i", new_line_no);
  if (sp_chk != (int) label_len + 1)
    {
      //There is an error, so exit the program.
      fprintf (stderr, "Print Error - \
Unable to print the correct characters to a string.\n");
      return -1;
    }

  gtk_label_set_text (GTK_LABEL (sen->line_no), (const char *) new_label);
  free (new_label);

  return 0;
}

/* Updates the line number and label of a sentence.
 *  input:
 *    sen - the sentence to be updated.
 *    new - the line number to be set.
 *  output:
 *    0 on success, -1 on error.
 */
int
sentence_update_line_no (sentence * sen, int new)
{
  int old, rc;
  old = SD(sen)->line_num;

  rc = sentence_set_line_no (sen, new);
  if (rc == -1)
    return -1;

  // The next part isn't necessary for a new sentence.
  if (old == 0)
    return 0;

  // This next part is only for aris proof sentences, not goal lines.
  sen_parent * sp;
  sp = sen->parent;
  if (sp->type == SEN_PARENT_TYPE_GOAL)
    return 0;

  int line_mod, i;

  line_mod = new - old;

  for (i = 0; i < SEN_DEPTH(sen); i++)
    {
      // Only the indices that are greater than the new line will
      // need to be changed.
      if (SEN_IND(sen,i) >= old)
	sentence_set_index (sen, i, SEN_IND(sen,i) + line_mod);
    }

  if (sentence_get_rule (sen) != RULE_LM)
    return 0;

  GtkWidget * menu_item, * menu, * submenu;
  GList * gl;

  gl = gtk_container_get_children (GTK_CONTAINER (sp->menubar));
  menu = g_list_nth_data (gl, RULES_MENU);
  submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (menu));

  gl = gtk_container_get_children (GTK_CONTAINER (submenu));
  for (; gl; gl = gl->next)
    {
      menu_item = gl->data;

      if (GTK_IS_SEPARATOR_MENU_ITEM (menu_item))
	continue;

      const char * label =
	gtk_menu_item_get_label (GTK_MENU_ITEM (gl->data));

      int chk, line_num, lbl_len;
      char * file_name;

      lbl_len = strlen (label);

      file_name = (char *) calloc (lbl_len, sizeof (char));
      CHECK_ALLOC (file_name, -1);

      chk = sscanf (label, "%i - %s", &line_num, file_name);
      if (chk != 2)
	continue;

      if (line_num < old)
	continue;

      line_num = new;

      char * new_label;
      int alloc_size;

      alloc_size = log10 (line_num) + strlen (file_name) + 4;
      new_label = (char *) calloc (alloc_size + 1, sizeof (char));
      CHECK_ALLOC (new_label, -1);

      sprintf (new_label, "%i - %s", line_num, file_name);
      free (file_name);

      gtk_menu_item_set_label (GTK_MENU_ITEM (gl->data), new_label);
    }

  return 0;
}

/* Add a reference to the proof.
 *  input:
 *    sen - the sentence to which to add a reference.
 *    ref - the reference to add.
 *  output:
 *    0 on success, -1 on memory error.
 */
int
sentence_add_ref (sentence * sen, sentence * ref)
{
  item_t * itm;
  itm = ls_push_obj (sen->references, ref);
  if (!itm)
    return -1;

  if (SD(sen)->refs)
    free (SD(sen)->refs);
  
  SD(sen)->refs = (short *) calloc (sen->references->num_stuff + 1,
				    sizeof (short));
  CHECK_ALLOC (SD(sen)->refs, -1);
  int i = 0;
  for (itm = sen->references->head; itm; itm = itm->next, i++)
    {
      sen_data * sd = SD(itm->value);
      SD(sen)->refs[i] = sd->line_num;
    }
  SD(sen)->refs[i] = -1;

  return 0;
}

/* Remove a reference from the proof.
 *  input:
 *    sen - the sentence from which to remove a reference.
 *    ref - the reference to be removed.
 *  output:
 *    0 on success, -1 on memory error.
 */
int
sentence_rem_ref (sentence * sen, sentence * ref)
{
  if (sen->references)
    ls_rem_obj_value (sen->references, ref);

  if (SD(sen)->refs)
    free (SD(sen)->refs);

  item_t * itm;

  SD(sen)->refs = (short *) calloc (sen->references->num_stuff + 1,
				    sizeof (short));
  CHECK_ALLOC (SD(sen)->refs, -1);
  int i = 0;
  for (itm = sen->references->head; itm; itm = itm->next, i++)
    {
      sen_data * sd = SD(itm->value);
      SD(sen)->refs[i] = sd->line_num;
    }
  SD(sen)->refs[i] = -1;

  return 0;
}

/* Update the references of a sentence from data.
 *  input:
 *    sen - the sentence of which to update the references.
 *  output:
 *    0 on success, -1 on memory error.
 */
int
sentence_update_refs (sentence * sen)
{
  int i, ln;
  ln = sentence_get_line_no (sen);

  if (SD(sen)->refs)
    {
      for (i = 0; SD(sen)->refs[i] != -1; i++)
	{
	  int cur_line;
	  item_t * ev_itr;

	  cur_line = SD(sen)->refs[i];
	  ev_itr = sen->parent->everything->head;

	  if (cur_line > ln)
	    continue;

	  for (; ev_itr != NULL; ev_itr = ev_itr->next)
	    {
	      sentence * ref_sen;
	      ref_sen = ev_itr->value;

	      if (sentence_get_line_no (ref_sen) == cur_line)
		{
		  item_t * ret;
		  ret = ls_push_obj (sen->references, ref_sen);
		  if (!ret)
		    return -1;

		  break;
		}
	    }
	}
    }
  return 0;
}

/* Sets the font of a sentence.
 *  input:
 *    sen - The sentence to set the font of.
 *    font - The new font.
 *  output:
 *    none.
 */
void
sentence_set_font (sentence * sen, int font)
{
  int font_size;

  sen->font_resizing = 1;
  font_size = pango_font_description_get_size (the_app->fonts[font]);
  font_size /= PANGO_SCALE;

  sentence_resize_text (sen, font_size);
  LABEL_SET_FONT (sen->line_no, the_app->fonts[font]);
  ENTRY_SET_FONT (sen->entry, the_app->fonts[font]);
  LABEL_SET_FONT (sen->rule_box, the_app->fonts[font]);

  sen->font_resizing = 0;
}

/* Resizes the pixbufs of a sentence.
 *  input:
 *    sen - the sentence to be resized.
 *    new_size - the font size to which to set the sentence.
 *  output:
 *    0 on success.
 */
int
sentence_resize_text (sentence * sen, int new_size)
{
  GtkTextIter iter;
  GtkTextBuffer * buffer;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));
  gtk_text_buffer_get_start_iter (buffer, &iter);

  while (!gtk_text_iter_is_end (&iter))
    {
      GdkPixbuf * pix;
      pix = gtk_text_iter_get_pixbuf (&iter);
      if (pix)
	{
	  GdkPixbuf * new_pix, * old_pix;
	  char * val;
	  val = (char *) g_object_get_data (G_OBJECT (pix), _("conn"));

	  new_pix = sen_parent_get_conn_by_type (sen->parent,
						 val);

	  GtkTextIter next;
	  next = iter;
	  gtk_text_iter_forward_char (&next);
	  gtk_text_buffer_delete (buffer, &iter, &next);
	  gtk_text_buffer_insert_pixbuf (buffer, &iter,
					 new_pix);
	}
      else
	{
	  gtk_text_iter_forward_char (&iter);
	}
    }

  return 0;
}

/* Sets the background color of a sentence.
 *  input:
 *    sen - the sentence to change the background color of.
 *    bg_color - the index in the_app->bg_colors.
 *  output:
 *    none
 */
void
sentence_set_bg (sentence * sen, int bg_color)
{
  COLOR_TYPE inv;
  INVERT (the_app->bg_colors[bg_color], inv);
  gtk_widget_override_background_color (sen->entry, GTK_STATE_NORMAL,
			the_app->bg_colors[bg_color]);
  gtk_widget_override_background_color (sen->entry, GTK_STATE_NORMAL,
			  the_app->bg_colors[bg_color]);
  gtk_widget_override_background_color (sen->entry, GTK_STATE_SELECTED, inv);
  sen->bg_color = bg_color;
  free (inv);
}

/* Sets the evaluation value of a sentence.
 *  input:
 *    sen - the sentence to change the value of.
 *    value_type - the value type to change it to.
 *  output:
 *    none.
 */
void
sentence_set_value (sentence * sen, int value_type)
{
  sen->value_type = value_type;
  gtk_image_set_from_icon_name (GTK_IMAGE (sen->value), sen_values [value_type], GTK_ICON_SIZE_MENU);
}

/* Returns the line number of a sentence.
 */
int
sentence_get_line_no (sentence * sen)
{
  return SD(sen)->line_num;
}

/* Returns the position in the containing grid of a sentence.
 */
int
sentence_get_grid_no (sentence * sen)
{
  int line_num;
  sen_parent * sp;
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);
  sp = sen->parent;
  gtk_container_child_get_property (GTK_CONTAINER (sp->container),
				   sen->panel,
				   "top-attach",
				    &val);
  line_num = g_value_get_int (&val);
  return line_num;
}

/* Connects the callback signals to a sentence.
 *  input:
 *    sen - the sentence to connect signals to.
 *  output:
 *    none.
 */
void
sentence_connect_signals (sentence * sen)
{
  g_signal_connect (G_OBJECT (sen->entry), "focus-in-event",
		    G_CALLBACK (sentence_focus_in), (gpointer) sen);
  g_signal_connect (G_OBJECT (sen->entry), "focus-out-event",
		    G_CALLBACK (sentence_focus_out), (gpointer) sen);
  g_signal_connect (G_OBJECT (sen->entry), "button-press-event",
		    G_CALLBACK (sentence_btn_press), (gpointer) sen);
  g_signal_connect (G_OBJECT (sen->entry), "button-release-event",
		    G_CALLBACK (sentence_btn_release), (gpointer) sen);
  g_signal_connect (G_OBJECT (sen->entry), "key-press-event",
		    G_CALLBACK (sentence_key_press), (gpointer) sen);
  g_signal_connect (G_OBJECT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry))),
		    "changed",
		    G_CALLBACK (sentence_changed), (gpointer) sen);
  sen->sig_id = g_signal_connect (G_OBJECT (sen->entry), "size-allocate",
				  G_CALLBACK (sentence_mapped), (gpointer) sen);
}

/* Selects the references and rule of a sentence when it is selected.
 *  input:
 *    sen - the sentence that has just been selected.
 *  output:
 *    0 on success.
 */
int
sentence_in (sentence * sen)
{
  sen_parent * sp = sen->parent;
  item_t * e_itr = ls_find (sp->everything, sen);
  // Find the item in everything that corresponds to this sentence.

  sp->focused = e_itr;

  // Set the background color to cyan.
  sentence_set_bg (sen, BG_COLOR_CONC);

  if (!SEN_PREM(sen))
    {
      int rule = sentence_get_rule (sen);
      // Toggle the rule, if one exists.
      if (rule != -1)
	{
	  // Find which toggle button corresponds to this rule.
	  if (the_app->rt->toggled != rule)
	    {
	      the_app->rt->user = 0;
	      TOGGLE_BUTTON (the_app->rt->rules[rule]);
	      the_app->rt->user = 1;
	    }
	}
      else if (the_app->rt->toggled != -1)
	{
	  the_app->rt->user = 0;
	  TOGGLE_BUTTON (the_app->rt->rules[the_app->rt->toggled]);
	  the_app->rt->user = 1;
	}

      // Set the background color of the references.
      item_t * r_itr = sen->references->head;
      for (; r_itr; r_itr = r_itr->next)
	{
	  int entire = 1;

	  entire = sentence_check_entire (sen, r_itr->value);
	  sentence_set_reference (SENTENCE (r_itr->value), 1, entire);
	}
    }
  else if (sp->type == SEN_PARENT_TYPE_PROOF
	   && the_app->rt->toggled != -1)
    {
      the_app->rt->user = 0;
      TOGGLE_BUTTON (the_app->rt->rules[the_app->rt->toggled]);
      the_app->rt->user = 1;
    }

  return 0;
}

/* Deselects the references and rule of a sentence when it is deselected.
 *  input:
 *    sen - the newly deselected sentence.
 *  output:
 *    0 on success, -1 on memory error.
*/
int
sentence_out (sentence * sen)
{
  sen_parent * sp = sen->parent;
  if (!sp)
    return -1;

  GtkTextBuffer * buffer;
  GtkTextIter start, end;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  gtk_text_buffer_remove_tag_by_name (buffer, "hilight", &start, &end);
  gtk_text_buffer_remove_tag_by_name (buffer, "negative", &start, &end);

  // Reset the background color.
  // Reset the background color of the references.
  sentence_set_bg (sen, BG_COLOR_DEFAULT);

  if (!SEN_PREM(sen))
    {

      item_t * ref_itr = sen->references->head;
      for (; ref_itr; ref_itr = ref_itr->next)
	{
	  int entire;

	  entire = sentence_check_entire (sen, ref_itr->value);
	  sentence_set_reference (SENTENCE (ref_itr->value), 0, entire);
	}
    }

  return 0;
}

/* Selects a sentence as a reference.
 *  input:
 *    sen - the sentence that has just been selected.
 *  output:
 *    0 on success, -1 on error, -2 on memory error.
 */
int
select_reference (sentence * sen)
{
  // Confirm that focused exists.
  // Determine whether or not this sentence is already a reference.
  // Add/remove sen to/from focused's references.
  // Set the background color of sen.

  sen_parent * sp = sen->parent;

  if (the_app->verbose)
    printf ("Selecting reference.\n");

  if (!sp->focused || SEN_PREM(sp->focused->value))
    return -1;

  sentence * fcs_sen;
  fcs_sen = sp->focused->value;

  if (sentence_get_line_no (sen) >= sentence_get_line_no (fcs_sen))
    {
      if (the_app->verbose)
	printf ("Must select reference that comes before focused.\n");
      return -1;
    }

  sentence * ref_sen = sen;
  int entire, ret;

  // Get indices of each.
  // This isn't necessary if the sentence is a premise, or if it has depth == zero.
  ret = sentence_can_select_as_ref (fcs_sen, sen);
  if (ret < 0)
    {
      entire = 1;
      ret *= -1;
    }
  else
    {
      entire = 0;
    }

  item_t * ref_itr;
  ref_itr = ls_nth (sp->everything, ret - 1);
  ref_sen = ref_itr->value;

  if (ref_sen->reference)
    {
      // Remove sen from focused's references.
      if (the_app->verbose)
	printf ("Removing reference.\n");

      sentence_rem_ref (fcs_sen, ref_sen);
      sentence_set_reference (ref_sen, 0, entire);
    }
  else
    {
      // Add sen to focused's references.
      if (the_app->verbose)
	printf ("Adding reference.\n");

      sentence_add_ref (fcs_sen, ref_sen);
      sentence_set_reference (ref_sen, 1, entire);
    }

  if (sp->type == SEN_PARENT_TYPE_PROOF)
    {
      undo_info ui;
      ui.type = -1;
      ret = aris_proof_set_changed ((aris_proof *) sp, 1, ui);
      if (ret < 0)
	return -2;
    }

  return 0;
}

/* Selects a sentence.
 *  input:
 *    sen - the sentence being selected.
 *  output:
 *    0 on success, -1 on error (memory).
 */
int
select_sentence (sentence * sen)
{
  sen_parent * sp;

  sp = sen->parent;

  if (sp->type == SEN_PARENT_TYPE_GOAL)
    return 0;

  if (!ARIS_PROOF (sp)->selected)
    return 0;

  if (sen->selected)
    {
      if (the_app->verbose)
	printf ("Deselecting sentence.\n");

      ls_rem_obj_value (ARIS_PROOF (sp)->selected, sen);
      if (SEN_SUB(sen))
	{
	  // Remove the entire subproof.
	  int sen_depth;
	  sen_depth = SEN_DEPTH(sen);

	  item_t * ev_itr;
	  ev_itr = ls_find (sp->everything, sen);
	  for (; ev_itr; ev_itr = ev_itr->next)
	    {
	      sentence * ev_sen;
	      ev_sen = ev_itr->value;
	      if (SEN_DEPTH(ev_sen) < SEN_DEPTH(sen))
		break;

	      ls_rem_obj_value (ARIS_PROOF (sp)->selected, ev_sen);
	    }
	}
      sentence_set_selected (sen, 0);
    }
  else
    {
      if (the_app->verbose)
	printf ("Selecting sentence.\n");

      item_t * itm;
      itm = ls_push_obj (ARIS_PROOF (sp)->selected, sen);
      if (!itm)
	return -1;

      if (SEN_SUB(sen))
	{
	  // Add entire subproof.
	  int sen_depth;
	  sen_depth = SEN_DEPTH(sen);

	  item_t * ev_itr;
	  ev_itr = ls_find (sp->everything, sen);
	  for (ev_itr = ev_itr->next; ev_itr; ev_itr = ev_itr->next)
	    {
	      sentence * ev_sen;
	      ev_sen = ev_itr->value;
	      if (SEN_DEPTH(ev_sen) < sen_depth)
		break;

	      itm = ls_push_obj (ARIS_PROOF (sp)->selected, ev_sen);
	      if (!itm)
		return -1;
	    }
	}

      sentence_set_selected (sen, 1);
    }

  return 0;
}

/* Gets the index of a text iter overall.
 *  input:
 *    iter - the text iter of which to get the index.
 *  output:
 *    The index in the buffer of the text iter.
 */
int
get_index (GtkTextIter * iter)
{
  GtkTextBuffer * buffer;
  int ret, line, i;

  buffer = gtk_text_iter_get_buffer (iter);
  line = gtk_text_iter_get_line (iter);
  ret = 0;

  for (i = 0; i < line; i++)
    {
      GtkTextIter tmp_itr;
      gtk_text_buffer_get_iter_at_line (buffer, &tmp_itr, i);
      ret += gtk_text_iter_get_chars_in_line (&tmp_itr);
    }

  ret += gtk_text_iter_get_line_offset (iter);
  return ret;
}

/* Gets the iterator of the text buffer at the given index.
 *  input:
 *    buffer - the text buffer.
 *    iter - a pointer to the text iterator to get the iterator.
 *    index - the index of which to get the iterator.
 *  output:
 *    none.
 */
void
get_iter_at_index (GtkTextBuffer * buffer, GtkTextIter * iter, int index)
{
  int i, offset;
  offset = i = 0;

  while (1)
    {
      GtkTextIter tmp_itr;
      int tmp;

      gtk_text_buffer_get_iter_at_line (buffer, &tmp_itr, i);
      tmp = gtk_text_iter_get_chars_in_line (&tmp_itr);
      if (tmp + offset >= index)
	break;
      offset += tmp;
      i++;
    }

  gtk_text_buffer_get_iter_at_line_offset (buffer, iter, i,
					   index - offset);
}


/* Processes a key press for a sentence.
 *  input:
 *    sen - the sentence being processed.
 *    key - the keycode for the key being pressed.
 *    ctrl - whether or not ctrl is being held.
 *  output:
 *    1 if the event should be propogated, 0 otherwise.
 */
int
sentence_key (sentence * sen, int key, int ctrl)
{
  sen_parent * sp = sen->parent;
  int ret = 1;
  GtkTextBuffer * buffer;
  GtkTextIter start, end;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  gtk_text_buffer_remove_tag_by_name (buffer, "hilight", &start,
				      &end);
  gtk_text_buffer_remove_tag_by_name (buffer, "negative", &start,
				      &end);

  int tmp_pos, offset;

  if (ctrl)
    {
      char * insert_char = NULL;
      GdkPixbuf * pixbuf = NULL;
      int font_size;

      font_size =
	pango_font_description_get_size (the_app->fonts[sen->parent->font]);
      font_size /= PANGO_SCALE;

      switch (key)
	{
	case GDK_KEY_7:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, AND);
	  break;
	case GDK_KEY_backslash:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, OR);
	  break;
	case GDK_KEY_grave:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, NOT);
	  break;
	case GDK_KEY_4:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, CON);
	  break;
	case GDK_KEY_5:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, BIC);
	  break;
	case GDK_KEY_2:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, UNV);
	  break;
	case GDK_KEY_3:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, EXL);
	  break;
	case GDK_KEY_1:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, TAU);
	  break;
	case GDK_KEY_6:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, CTR);
	  break;
	case GDK_KEY_semicolon:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, ELM);
	  break;
	case GDK_KEY_period:
	  pixbuf = sen_parent_get_conn_by_type (sen->parent, NIL);
	  break;
	case GDK_KEY_space:
	  insert_char = " ";
	  break;
 	default:
	  break;
	}

      if (pixbuf)
	{
	  GtkTextIter iter;
	  GtkTextMark * mark;

	  mark = gtk_text_buffer_get_insert (buffer);
	  gtk_text_buffer_get_iter_at_mark (buffer, &iter, mark);
	  gtk_text_buffer_insert_pixbuf (buffer, &iter, pixbuf);
	}

      if (insert_char)
	gtk_text_buffer_insert_at_cursor (buffer, insert_char, -1);
    }
  else
    {
      item_t * dst;
      GtkTextIter pos, chk_pos, c_pos;
      gunichar o_char, c_char;
      int got_par = 0, mod = 0, chk;

      gtk_text_buffer_get_iter_at_mark (buffer, &pos,
					gtk_text_buffer_get_insert (buffer));
      chk_pos = pos;

      switch (key)
	{
	case GDK_KEY_Up:
	  dst = (sp->focused == sp->everything->head)
	    ? sp->everything->tail : sp->focused->prev;
	  if (the_app->verbose)
	    printf ("Got Key Up\n");
	  gtk_widget_grab_focus (SENTENCE (dst->value)->entry);
	  ret = 0;
	  break;
	case GDK_KEY_Down:
	  dst = (sp->focused == sp->everything->tail)
	    ? sp->everything->head : sp->focused->next;
	  if (the_app->verbose)
	    printf ("Got Key Down\n");
	  gtk_widget_grab_focus (SENTENCE (dst->value)->entry);
	  ret = 0;
	  break;
	case GDK_KEY_Left:
	case GDK_KEY_Right:
	  if (the_app->verbose)
	    {
	      if (key == GDK_KEY_Left)
		printf ("Got Key Left\n");
	      else
		printf ("Got Key Right\n");
	    }

	  chk = (key == GDK_KEY_Left)
	    ? gtk_text_iter_backward_char (&chk_pos)
	    : gtk_text_iter_forward_char (&chk_pos);

	  break;
	}

      if (key != GDK_KEY_Up && key != GDK_KEY_Down)
	{
	  c_pos = chk_pos;
	  gtk_text_iter_backward_char (&c_pos);
	  o_char = gtk_text_iter_get_char (&chk_pos);
	  c_char = gtk_text_iter_get_char (&c_pos);
	  if (o_char == '(')
	    {
	      got_par = 1;
	      pos = chk_pos;
	      mod = 0;
	    }

	  if (c_char == ')')
	    {
	      got_par = 2;
	      pos = c_pos;
	      mod = -1;
	    }
	}
	  
      if (got_par)
	{
	  unsigned char * tmp_str = NULL;
	  GtkTextIter oth_pos;
	  unsigned char * sen_text;
	  sen_text = sentence_get_text (sen);

	  offset = get_index (&pos);

	  if (got_par == 1)
	    {
	      tmp_pos = parse_parens (sen_text, offset, NULL);
	    }
	  else
	    {
	      tmp_pos = reverse_parse_parens (sen_text, offset,
					      NULL);
	      if (tmp_pos == -2)
		return -1;
	    }

	  GtkTextIter semi;
	  semi = pos;
	  gtk_text_iter_forward_char (&semi);

	  if (tmp_pos < 0)
	    {
	      gtk_text_buffer_apply_tag_by_name (buffer, "negative",
						 &pos, &semi);
	    }
	  else
	    {
	      gtk_text_buffer_apply_tag_by_name (buffer, "hilight",
						 &pos, &semi);
	      get_iter_at_index (buffer, &oth_pos, tmp_pos);
	      semi = oth_pos;
	      gtk_text_iter_forward_char (&semi);
	      gtk_text_buffer_apply_tag_by_name (buffer, "hilight",
						 &oth_pos, &semi);
	    }
	}
    }
  return ret;
}


/* Sets the reference state of a sentence.
 *  input:
 *    sen - the sentence to set the reference state of.
 *    reference - whether this sentence is being added or removed as a reference.
 *    entire_subproof - whether or not the entire subproof should be selected.
 *  output:
 *    none.
 */
void
sentence_set_reference (sentence * sen, int reference, int entire_subproof)
{
  if (reference)
    sentence_set_bg (sen, BG_COLOR_REF);
  else
    sentence_set_bg (sen, BG_COLOR_DEFAULT);

  if (SEN_SUB(sen) && entire_subproof)
    {
      item_t * sub_itr;

      sub_itr = ls_find (sen->parent->everything, sen);

      for (sub_itr = sub_itr->next; sub_itr; sub_itr = sub_itr->next)
	{
	  sentence * sub_sen;
	  sub_sen = sub_itr->value;

	  if (SEN_DEPTH(sub_sen) < SEN_DEPTH(sen))
	    break;

	  if (reference)
	    sentence_set_bg (sub_sen, BG_COLOR_REF);
	  else
	    sentence_set_bg (sub_sen, BG_COLOR_DEFAULT);
	}
    }

  sen->reference = reference;
}

/* Sets the selected state of a sentence.
 *  input:
 *   sen - The sentence that is being selected.
 *   selected - Whether or not the sentence is being selected.
 *  output:
 *   none.
 */
void
sentence_set_selected (sentence * sen, int selected)
{
  if (selected)
    sentence_set_bg (sen, BG_COLOR_SEL);
  else
    sentence_set_bg (sen, BG_COLOR_DEFAULT);

  if (SEN_SUB(sen))
    {
      item_t * sub_itr;

      sub_itr = ls_find (sen->parent->everything, sen);

      for (sub_itr = sub_itr->next; sub_itr; sub_itr = sub_itr->next)
	{
	  sentence * sub_sen;
	  sub_sen = sub_itr->value;

	  if (SEN_DEPTH(sub_sen) < SEN_DEPTH(sen))
	    break;

	  if (selected)
	    sentence_set_bg (sub_sen, BG_COLOR_SEL);
	  else
	    sentence_set_bg (sub_sen, BG_COLOR_DEFAULT);
	}
    }

  sen->selected = selected;
}

/* Gets the correct text from a sentence text view.
 *  input:
 *    sen - the sentence from which to get the text.
 *  output:
 *    the text of the sentence, or NULL on error.
 */
char *
sentence_copy_text (sentence * sen)
{
  char * ret_str;

  GtkTextBuffer * buffer;
  GtkTextIter start, end;
  int text_len;
  int i;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));
  gtk_text_buffer_get_bounds (buffer, &start, &end);

  i = 0;
  ret_str = (char *) calloc (1, sizeof (char));
  CHECK_ALLOC (ret_str, NULL);

  while (!gtk_text_iter_is_end (&start))
    {
      GdkPixbuf * pixbuf;
      pixbuf = gtk_text_iter_get_pixbuf (&start);
      if (pixbuf)
	{
	  // Determine what this is, and add the
	  // corresponding character to ret_str;
	  int alloc_size;
	  unsigned char * new_char;
	  const char * val;

	  val = (char *) g_object_get_data (G_OBJECT (pixbuf),
					    _("conn"));
	  ret_str = (char *) realloc (ret_str,
				      (i + CL + 1) * sizeof (char));
	  CHECK_ALLOC (ret_str, NULL);
	  strcpy (ret_str + i, val);
	  i += CL;
	}
      else
	{
	  char * c;
	  end = start;
	  gtk_text_iter_forward_char (&end);

	  c = gtk_text_iter_get_text (&start, &end);

	  ret_str = (char *) realloc (ret_str, (i + 2)
				      * sizeof (char));
	  CHECK_ALLOC (ret_str, NULL);
	  
	  ret_str[i++] = *c;
	}

      gtk_text_iter_forward_char (&start);
    }

  ret_str[i] = '\0';

  // Handle comments.
  // Will need to fix this up, since this means that comments aren't saved.

  unsigned char * semi_str, * fin_str;
  semi_str = strchr (ret_str, ';');
  if (semi_str)
    {
      int ret_len;
      semi_str[0] = '\0';
      ret_len = strlen (ret_str);
      fin_str = (unsigned char *) calloc (ret_len + 1, sizeof (char));
      CHECK_ALLOC (fin_str, NULL);
      memcpy (fin_str, ret_str, ret_len);
      free (ret_str);
      ret_str = fin_str;
    }

  return ret_str;
}

/* Sets the text of the text view of a sentence from the text.
 *  input:
 *    sen - the sentence of which to set the text.
 *  output:
 *    0 on success, -1 on memory error.
 */
int
sentence_paste_text (sentence * sen)
{
  int i;

  GtkTextBuffer * buffer;
  GtkTextIter end;

  unsigned char * sen_text = sentence_get_text (sen);
  
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));
  gtk_text_buffer_get_start_iter (buffer, &end);

  for (i = 0; sen_text[i]; i++)
    {
      if (IS_TYPE_CONN (sen_text + i, gui_conns)
	  || IS_TYPE_CONN (sen_text + i, cli_conns))
	{
	  GdkPixbuf * pix, * new_pix;

	  unsigned char * conn;
	  int len;

	  if (IS_TYPE_CONN (sen_text + i, gui_conns))
	    {
	      if (!strncmp (sen_text + i, gui_conns.not, gui_conns.nl))
		len = gui_conns.nl;
	      else
		len = gui_conns.cl;
	    }
	  else
	    {
	      if (!strncmp (sen_text + i, NOT, NL))
		len = NL;
	      else
		len = CL;
	    }

	  conn = (unsigned char *) calloc (len + 1, sizeof (char));
	  CHECK_ALLOC (conn, -1);
	  
	  strncpy (conn, sen_text + i, len);
	  conn[len] = '\0';

	  pix = sen_parent_get_conn_by_type (sen->parent,
					     conn);
	  free (conn);

	  if (!pix)
	    return -2;

	  // No need to resize this, since sentence_set_font
	  // will be called after this, and it handles it instead.

	  gtk_text_buffer_insert_pixbuf (buffer, &end, pix);
	  i += len - 1;
	}
      else
	{
	  gtk_text_buffer_insert (buffer, &end, sen_text + i, 1);
	}
    }

  return 0;
}

/* Processes a text change in a sentence.
 *  input:
 *    sen - the sentence to process a change in.
 *  ouput:
 *    0 on success, -1 on memory error.
 */
int
sentence_text_changed (sentence * sen)
{
  if (sen->font_resizing || sen->parent->undo)
    return 0;

  int ln;
  ln = sentence_get_line_no (sen);

  sen_parent * sp = sen->parent;
  sentence_set_value (sen, VALUE_TYPE_BLANK);

  item_t * e_itr = ls_find (sp->everything, sen);

  for (e_itr = sp->everything->head; e_itr; e_itr = e_itr->next)
    {
      list_t * e_refs;
      e_refs = SENTENCE (e_itr->value)->references;
      if (e_refs)
	{
	  item_t * r_itr;
	  r_itr = ls_find (e_refs, sen);

	  if (r_itr)
	    sentence_set_value (SENTENCE (e_itr->value), VALUE_TYPE_BLANK);
	}
    }

  if (SD(sen)->sexpr)
    {
      free (SD(sen)->sexpr);
      SD(sen)->sexpr = NULL;
    }

  char * text;
  GtkTextBuffer * buffer;
  GtkTextIter start, end, semi;
  int text_len, old_len;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sen->entry));
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = sentence_copy_text (sen);
  text_len = strlen (text);

  unsigned char * sen_text;
  int diff_pos;

  sen_text = sentence_get_text (sen);
  old_len = strlen (sen_text);
  diff_pos = find_difference (sen_text, (unsigned char *) text);

#if 0
    {
      //TODO: Check for semi-colon and set mark.
      if (text_len > old_len)
	{
	  if (sen->text[diff_pos] == ';')
	    {
	      GtkTextIter cur_pos;
	      gtk_text_buffer_get_iter_at_mark (buffer, &cur_pos,
						gtk_text_buffer_get_insert (buffer));
	  
	      if (sen->mark)
		{
		  gtk_text_buffer_get_iter_at_mark (buffer, &semi, sen->mark);

		  int mark_off, cur_off;
		  mark_off = gtk_text_iter_get_offset (&semi);
		  cur_off = gtk_text_iter_get_offset (&cur_pos);

		  if (cur_off < mark_off)
		    {
		      gtk_text_buffer_move_mark (buffer, sen->mark, &cur_pos);
		    }
		}
	      else
		{
		  sen->mark = gtk_text_mark_new (SEMI_NAME, TRUE);
		  gtk_text_buffer_add_mark (buffer, sen->mark, &cur_pos);
		}
	    }
	}
      else
	{
	  if (text[diff_pos] == ';')
	    {
	      GtkTextIter cur_pos;
	      gtk_text_buffer_get_iter_at_mark (buffer, &cur_pos,
						gtk_text_buffer_get_insert (buffer));
	    }
	}
    }
#endif

  undo_info ui;
  ui = undo_info_init_one (NULL, sen, UIT_MOD_TEXT);
  if (ui.type == -1)
    return -1;

  if (sp->type == SEN_PARENT_TYPE_PROOF)
    {
      int ret;

      ret = aris_proof_set_changed (ARIS_PROOF (sp), 1, ui);
      if (ret < 0)
	return -1;

      gtk_widget_override_background_color (sen->eventbox, GTK_STATE_NORMAL, NULL);

      if (SEN_PARENT (ARIS_PROOF (sp)->goal)->everything->num_stuff > 0)
	{
	  item_t * mod_itm;
	  mod_itm = SEN_PARENT (ARIS_PROOF (sp)->goal)->everything->head;
	  for (; mod_itm; mod_itm = mod_itm->next)
	    {
	      int m_ln;
	      m_ln = sentence_get_line_no (mod_itm->value);
	      if (m_ln == ln)
		{
		  sentence_update_line_no (SENTENCE (mod_itm->value), -1);
		  sentence_set_value (SENTENCE (mod_itm->value), VALUE_TYPE_BLANK);
		  break;
		}
	    }
	}
    }
  else
    {
      // Otherwise, it's a goal.
      int ret;

      ret = aris_proof_set_changed (GOAL (sp)->parent, 1, ui);
      if (ret < 0)
	return -1;

      item_t * mod_itm;
      sentence * mod_sen;
      mod_itm = ls_nth ((SEN_PARENT (GOAL (sp)->parent)->everything), ln);
      if (mod_itm)
	{
	  mod_sen = mod_itm->value;
	  gtk_widget_override_background_color (mod_sen->eventbox, GTK_STATE_NORMAL, NULL);
	  sentence_set_line_no (sen, -1);
	}
    }

  if (sen_text)
    free (sen_text);
  SD(sen)->text = NULL;

  sentence_set_text (sen, text);
  free (text);
  return 0;
}

/* Checks if only the subproof premise or the entire subproof should be selected.
 *  input:
 *    sen - the sentence that is adding a reference.
 *    ref - the sentence being added as a reference.
 *  output:
 *    0 if only the premise should be selected, 1 otherwise.
 */
int
sentence_check_entire (sentence * sen, sentence * ref)
{
  if (!SEN_SUB(ref))
    return 0;

  if (SEN_DEPTH(ref) > SEN_DEPTH(sen))
    return 1;

  int i;
  for (i = 0; i < SEN_DEPTH(ref); i++)
    {
      if (SEN_IND(ref,i) != SEN_IND(sen,i))
	break;
    }

  int ret;
  if (SEN_IND(ref,i) == -1)
    ret = 0;
  else
    ret = 1;

  return ret;
}

/* Checks a sentence's rule against the boolean rules.
 *  input:
 *    sen - the sentenc being checked.
 *    boolean - whether or not boolean mode is being activated.
 *  output:
 *    0 if the rule does not check out, 1 if it does.
 */
int
sentence_check_boolean_rule (sentence * sen, int boolean)
{
  if (!boolean)
    return 1;

  int bool_okay = 1;
  int rule = sentence_get_rule (sen);

  if (rule < RULE_DM || rule == RULE_EQ || rule == RULE_EP
      || (rule >= RULE_EG && rule <= RULE_SP))
    bool_okay = 0;

  return bool_okay;
}

/* Determines whether or not one sentence can select the other as a reference.
 *  input:
 *    sen - The sentence looking to select a reference.
 *    ref - The sentence that is attempting to be selected.
 *  output:
 *    0 if ref cannot be selected by sen
 *    The common line between them,
 *    The negation of the common line if an entire subproof is being selected.
 */
int
sentence_can_select_as_ref (sentence * sen, sentence * ref)
{
  return sen_data_can_select_as_ref (SD(sen), SD(ref));
}

/* Sets the rule of a sentence and updates the rule box.
 */
int
sentence_set_rule (sentence * sen, int rule)
{
  SD(sen)->rule = rule;
  const char * rule_text = (SD(sen)->rule == -1)
    ? NULL : rules_list[SD(sen)->rule];
  gtk_label_set_text (GTK_LABEL (sen->rule_box),
		      rule_text);
  return 0;
}

/* Returns the rule of a sentence.
 */
int
sentence_get_rule (sentence  * sen)
{
  return SD(sen)->rule;
}

/* Sets the text of a sentence.
 */
int
sentence_set_text (sentence * sen, unsigned char * text)
{
  if (SD(sen)->text)
    free (SD(sen)->text);

  SD(sen)->text = strdup (text);

  if (!SD(sen)->text)
    return -1;
  return 0;
}

/* Returns the text of a sentence.
 */
unsigned char *
sentence_get_text (sentence * sen)
{
  return SD(sen)->text;
}

/* Returns whether or not the sentence is a premise.
 */
int
sentence_premise (sentence * sen)
{
  return SD(sen)->premise;
}

/* Returns whether or not the sentence is a subproof.
 */
int
sentence_subproof (sentence * sen)
{
  return SD(sen)->subproof;
}

/* Returns the depth of a sentence.
 */
int
sentence_depth (sentence * sen)
{
  return SD(sen)->depth;
}

/* Gets an index of a sentence.
 */
int
sentence_get_index (sentence * sen, int i)
{
  return SD(sen)->indices[i];
}

/* Sets an index of a sentence.
 */
int
sentence_set_index (sentence * sen, int i, int index)
{
  SD(sen)->indices[i] = index;
}
