/*
  form_type.c

  Copyright 2009, 2010, 2011, 2014, 2016 Free Software Foundation, Inc.

  This file is part of GNU Guile-Ncurses.

  Guile-Ncurses is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  Guile-Ncurses is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Guile-Ncurses.  If not, see
  <http://www.gnu.org/licenses/>.
*/
#include <config.h>

#include <assert.h>
#include <errno.h>
#include <libguile.h>
#include <stdio.h>
#include <string.h>

#if HAVE_CURSES_H
#include <curses.h>
#include <form.h>
#elif HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#include <ncurses/form.h>
#elif HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#include <ncursesw/form.h>
#else
#error "No curses.h file included"
#endif

#include "compat.h"
#include "form_func.h"
#include "form_type.h"
#include "gucuconfig.h"
#include "type.h"

scm_t_bits form_tag;
scm_t_bits field_tag;

SCM equalp_field (SCM x1, SCM x2);
size_t gc_free_field (SCM x);
SCM mark_field (SCM x);
int print_field (SCM x, SCM port, scm_print_state * pstate);


SCM equalp_form (SCM x1, SCM x2);
size_t gc_free_form (SCM x);
SCM mark_form (SCM x);
int print_form (SCM x, SCM port, scm_print_state * pstate);


// field -- in C, a FIELD.  In Scheme, a smob that contains the pointer

void
field_init_refcount (FIELD *field)
{
  set_field_userptr (field, (void *) 1);
}

bool
field_increase_refcount (FIELD *field)
{
  void *ptr = field_userptr (field);
  if (ptr >= (void *) INT_MAX)
    return FALSE;

  set_field_userptr (field, ptr + 1);
  return TRUE;
}

bool
field_decrease_refcount (FIELD *field)
{
  void *ptr = field_userptr (field);
  if (ptr == (void *) 0)
    return FALSE;
  set_field_userptr (field, ptr - 1);
  return TRUE;
}

int
field_get_refcount (FIELD *field)
{
  return (int) field_userptr (field);
}


int
_scm_is_field (SCM x)
{
  if (SCM_SMOB_PREDICATE (field_tag, x))
    {
      if (SCM_SMOB_DATA (x) != 0)
        return 1;
      else
        return 0;
    }
  else
    return 0;
}

FIELD *
_scm_to_field (SCM x)
{
  return (FIELD *) SCM_SMOB_DATA (x);
}

SCM
_scm_from_field (FIELD * x)
{
  SCM s_field;

  assert (x != NULL);

  SCM_NEWSMOB (s_field, field_tag, x);

  assert (x == (FIELD *) SCM_SMOB_DATA (s_field));

  if (0)
    {
      fprintf (stderr, "Making <#field> smob from FIELD * %p\n", (void *) x);
    }

  return (s_field);
}

// Fields are equal if they point to the same C structure
SCM
equalp_field (SCM x1, SCM x2)
{
  FIELD *field1 = (FIELD *) SCM_SMOB_DATA (x1);
  FIELD *field2 = (FIELD *) SCM_SMOB_DATA (x2);

  if ((field1 == NULL) || (field2 == NULL))
    return SCM_BOOL_F;
  else if ((field1 != field2))
    return SCM_BOOL_F;
  else
    return SCM_BOOL_T;
}

SCM
gucu_new_field (SCM height, SCM width, SCM top, SCM left, SCM offscreen,
                SCM nbuffers)
{
  SCM_ASSERT (scm_is_integer (height), height, SCM_ARG1, "new-field");
  SCM_ASSERT (scm_is_integer (width), width, SCM_ARG2, "new-field");
  SCM_ASSERT (scm_is_integer (top), top, SCM_ARG3, "new-field");
  SCM_ASSERT (scm_is_integer (left), left, SCM_ARG4, "new-field");
  SCM_ASSERT (scm_is_integer (offscreen), offscreen, SCM_ARG5, "new-field");
  SCM_ASSERT (scm_is_integer (nbuffers), nbuffers, SCM_ARG6, "new-field");

  int c_height = scm_to_int (height);
  int c_width = scm_to_int (width);
  int c_top = scm_to_int (top);
  int c_left = scm_to_int (left);
  int c_offscreen = scm_to_int (offscreen);
  int c_nbuffers = scm_to_int (nbuffers);

  FIELD *f =
    new_field (c_height, c_width, c_top, c_left, c_offscreen, c_nbuffers);
  if (f == NULL)
    {
      if (errno == E_BAD_ARGUMENT)
        {
          scm_misc_error ("new-field", "bad argument", SCM_EOL);
        }
      else if (errno == E_SYSTEM_ERROR)
        {
          scm_misc_error ("new-field", "system error", SCM_EOL);
        }
      else
        abort ();
    }

  /* This is a new field, so its refcount should be one. */
  field_init_refcount (f);

  SCM ret = _scm_from_field (f);

  return ret;
}


SCM
mark_field (SCM x UNUSED)
{
  // No SCMs in the field type: nothing to do here.
  return (SCM_BOOL_F);
}

/* The name is free_field.  The curses primitive that frees memory is
   called del_field. Note that del_field doesn't free the underlying
   window. */
size_t
gc_free_field (SCM field)
{
  FIELD *f = _scm_to_field (field);

  if (f != NULL)
    {
      field_decrease_refcount (f);
      if (field_get_refcount (f) == 0)
	{
	  /* No other #<field> or #<form> is using this field, so we
	     can free its data.  */
	  int ret = free_field (f);
	  if (ret != E_OK)
	    {
	      /* ??? --- what should happen when free fails? */
	    }
	}
      /* Detach the field's data from the field.  */
      SCM_SET_SMOB_DATA (field, NULL);
    }

  return 0;
}

int
print_field (SCM x, SCM port, scm_print_state * pstate UNUSED)
{
  FIELD *fld = (FIELD *) SCM_SMOB_DATA (x);
  char str[SIZEOF_VOID_P*2+3];

  scm_puts ("#<field ", port);

  if (fld == 0)
    scm_puts ("(freed)", port);
  else
    {
      if (snprintf (str, sizeof(str), "%p", (void *) fld) < 0)
        scm_puts ("???", port);
      else
        scm_puts (str, port);
    }

  scm_puts (">", port);

  // non-zero means success
  return 1;
}

SCM
gucu_is_field_p (SCM x)
{
  return scm_from_bool (_scm_is_field (x));
}

SCM
gucu_field_refcount (SCM x)
{
  SCM_ASSERT (_scm_is_field (x), x, SCM_ARG1, "%field-refcount");

  FIELD *m = _scm_to_field (x);
  return scm_from_int (field_get_refcount (m));
}

int
_scm_is_list_of_fields (SCM fields)
{
  int i, len;

  if (!scm_is_true (scm_list_p (fields)))
    return 0;
  len = scm_to_size_t (scm_length (fields));
  if (len == 0)
    return 0;
  for (i = 0; i < len; i ++)
    {
      SCM entry;
      entry = scm_list_ref (fields, scm_from_int (i));
      if (!_scm_is_field (entry))
	return 0;
    }
  return 1;
}

int
_scm_is_list_of_unattached_fields (SCM fields)
{
  int i, len;

  if (!scm_is_true (scm_list_p (fields)))
    return 0;
  len = scm_to_size_t (scm_length (fields));
  if (len == 0)
    return 0;
  for (i = 0; i < len; i ++)
    {
      SCM entry;
      entry = scm_list_ref (fields, scm_from_int (i));
      if (!_scm_is_field (entry))
	return 0;
      if (field_index (_scm_to_field (entry)) != ERR)
	return 0;
    }
  return 1;
}

// form -- in C, a FORM *.  In Scheme, a smob that contains the pointer
// to a form along with a list that contains the SCM of the fields

// N.B.: form->field must point to a C array containing the FIELD *
// contained in the SCM fields.

int
_scm_is_form (SCM x)
{
  return SCM_SMOB_PREDICATE (form_tag, x);
}

FORM *
_scm_to_form (SCM x)
{
  struct gucu_form *gf;

  scm_assert_smob_type (form_tag, x);

  gf = (struct gucu_form *) SCM_SMOB_DATA (x);

  return (FORM *) gf->form;
}


// Forms are equal if they point to the same C structure
SCM
equalp_form (SCM x1, SCM x2)
{
  FORM *form1 = (FORM *) SCM_SMOB_DATA (x1);
  FORM *form2 = (FORM *) SCM_SMOB_DATA (x2);

  if ((form1 == NULL) || (form2 == NULL))
    return SCM_BOOL_F;
  else if ((form1 != form2))
    return SCM_BOOL_F;
  else
    return SCM_BOOL_T;
}

SCM
mark_form (SCM x)
{
  struct gucu_form *gf;

  gf = (struct gucu_form *) SCM_SMOB_DATA (x);
  if (gf != NULL)
    {
      scm_gc_mark (gf->win_guard);
      scm_gc_mark (gf->sub_guard);
    }

  return SCM_BOOL_F;
}

size_t
gc_free_form (SCM x)
{
  struct gucu_form *form;
  int retval;

  form = (struct gucu_form *) SCM_SMOB_DATA (x);

  if (form != NULL && form->form != NULL)
    {
      FIELD **pfields = form_fields (form->form);

      retval = free_form (form->form);
      form->form = (FORM *) NULL;
      if (retval == E_BAD_ARGUMENT)
        {
          scm_error_scm (scm_from_locale_symbol ("ncurses"),
                         scm_from_locale_string ("garbage collection of form"),
                         scm_from_locale_string ("bad argument"),
                         SCM_BOOL_F, SCM_BOOL_F);
        }
      else if (retval == E_POSTED)
        {
          scm_error_scm (scm_from_locale_symbol ("ncurses"),
                         scm_from_locale_string ("garbage collection of form"),
                         scm_from_locale_string ("posted"),
                         SCM_BOOL_F, SCM_BOOL_F);
        }

      /* Decrease the refcount and maybe free the fields.  */
      while (*pfields != NULL)
	{
	  field_decrease_refcount (*pfields);
	  if (field_get_refcount (*pfields) == 0)
	    free_field (*pfields);
	  pfields ++;
	}

      /* Release the hold on any windows.  */
      form->win_guard = SCM_BOOL_F;
      form->sub_guard = SCM_BOOL_F;
    }
  SCM_SET_SMOB_DATA (x, NULL);

  return 0;
}

int
print_form (SCM x, SCM port, scm_print_state * pstate UNUSED)
{
  struct gucu_form *frm = (struct gucu_form *) SCM_SMOB_DATA (x);
  char str[SIZEOF_VOID_P*2+3];

  assert (frm != NULL);

  scm_puts ("#<form ", port);

  if (frm == NULL)
    scm_puts ("(freed)", port);
  else if (snprintf (str, sizeof(str), "%p", (void *) frm->form) < 0)
    scm_puts ("???", port);
  else
    scm_puts (str, port);

  scm_puts (">", port);

  // non-zero means success
  return 1;
}

SCM
gucu_is_form_p (SCM x)
{
  return scm_from_bool (_scm_is_form (x));
}


SCM
gucu_new_form (SCM fields)
{
  struct gucu_form *gf;
  size_t len;
  FIELD **c_fields;
  SCM smob;
  SCM entry;
  size_t i;

  // Step 0: check the inputs
  if (!_scm_is_list_of_fields (fields))
    scm_wrong_type_arg_msg ("new-form", SCM_ARG1, fields, "list of #<field>");
  if (!_scm_is_list_of_unattached_fields (fields))
    scm_misc_error ("new-form",
		    "fields may not be attached to more than one form: ~s",
		    scm_list_1 (fields));

  // Step 1: allocate memory
  len = scm_to_size_t (scm_length (fields));
  gf = scm_gc_malloc (sizeof (struct gucu_form), "gucu_form");
  c_fields = scm_gc_malloc (sizeof (FIELD *) * (len + 1), "gucu_form");

  // Step 2: initialize it with C code
  gf->form = 0;
  gf->win_guard = SCM_BOOL_F;
  gf->sub_guard = SCM_BOOL_F;

  // Step 3: Create the smob
  SCM_NEWSMOB (smob, form_tag, gf);

  // Step 4: Finish the initialization
  for (i = 0; i < len; i++)
    {
      entry = scm_list_ref (fields, scm_from_int (i));
      c_fields[i] = _scm_to_field (entry);
    }
  c_fields[len] = (FIELD *) NULL;

  gf->form = new_form (c_fields);

  if (gf->form == NULL)
    {
      free (c_fields);
      if (errno == E_BAD_ARGUMENT)
        {
          scm_error_scm (scm_from_locale_symbol ("ncurses"),
                         scm_from_locale_string ("new-form"),
                         scm_from_locale_string ("bad argument"),
                         fields, SCM_BOOL_F);
        }
      else if (errno == E_CONNECTED)
        {
          scm_error_scm (scm_from_locale_symbol ("ncurses"),
                         scm_from_locale_string ("new-form"),
                         scm_from_locale_string ("connected"),
                         SCM_BOOL_F, SCM_BOOL_F);
        }
      else if (errno == E_SYSTEM_ERROR)
        {
          scm_error_scm (scm_from_locale_symbol ("ncurses"),
                         scm_from_locale_string ("new-form"),
                         scm_from_locale_string ("system error"),
                         SCM_BOOL_F, SCM_BOOL_F);
        }
      else
        abort ();
    }

  for (i = 0; i < len; i++)
    {
      entry = scm_list_ref (fields, scm_from_int (i));
      field_increase_refcount (c_fields[i]);
    }

  return smob;
}

// Return the fields on which the form depends
SCM
gucu_form_fields (SCM form)
{
  struct gucu_form *gf;
  int i;

  scm_assert_smob_type (form_tag, form);

  gf = (struct gucu_form *) SCM_SMOB_DATA (form);
  if (gf == NULL || gf->form == NULL)
    return SCM_EOL;
  else
    {
      int len = field_count (gf->form);
      if (len == ERR || len == 0)
	return SCM_EOL;
      else
	{
	  FIELD **pfields;
	  pfields = form_fields (gf->form);
	  SCM list = SCM_EOL;
	  if (pfields == NULL)
	    return SCM_EOL;
	  else
	    {
	      for (i = 0; i < len; i ++)
		{
		  SCM entry;
		  field_increase_refcount (pfields[i]);
		  entry = _scm_from_field (pfields[i]);
		  list = scm_append (scm_list_2 (list, scm_list_1 (entry)));
		}
	      return list;
	    }
	}
    }
  return SCM_EOL;
}

// Detach the current fields from the form and replace them with
// a new list of fields.
SCM
gucu_set_form_fields_x (SCM form, SCM fields)
{
  SCM_ASSERT (_scm_is_form (form), form, SCM_ARG1, "set-form-fields!");

  struct gucu_form *gf;
  FIELD **c_fields;
  SCM entry;
  size_t i;
  int ret;

  if (!_scm_is_list_of_fields (fields))
    scm_wrong_type_arg_msg ("set-form-field!", SCM_ARG2, fields, "list of #<field>");

  /* FIXME: Check that all of the fields are either unattached or are attached to this
     form.  Is there a way to do that check?  */

  gf = (struct gucu_form *) SCM_SMOB_DATA (form);
  if (gf != NULL && gf->form != NULL)
    {
      FIELD **pfield_prev = NULL;
      int len_prev;
      int len_cur;

      /* Hold on to the old fields list.  */
      len_prev = field_count (gf->form);
      pfield_prev = form_fields (gf->form);

      /* Make a new fields list.  */
      len_cur = scm_to_int (scm_length (fields));
      c_fields = scm_gc_malloc (sizeof (FIELD *) * (len_cur + 1), "set-form-fields!");
      for (i = 0; i < len_cur; i++)
	{
	  entry = scm_list_ref (fields, scm_from_int (i));
	  c_fields[i] = _scm_to_field (entry);
	}
      c_fields[len_cur] = (FIELD *) NULL;

      /* Attach the new fields to the form.  */
      ret = set_form_fields (gf->form, c_fields);
      if (ret == E_BAD_ARGUMENT)
	scm_out_of_range ("set-form-fields!", fields);
      else if (ret == E_CONNECTED)
	form_connected_error ("set-form-fields!");
      else if (ret == E_POSTED)
	form_posted_error ("set-form-fields!");
      else if (ret == E_SYSTEM_ERROR)
	scm_syserror ("set-form-fields!");

      /* Increase the refcount on the new fields.  */
      for (i = 0; i < len_cur; i++)
	field_increase_refcount (c_fields[i]);

      /* Decrease the refcount on the old fields, and maybe free. */
      if (len_prev > 0 && pfield_prev != NULL)
	{
	  for (i = 0; i < len_prev; i ++)
	    {
	      field_decrease_refcount (pfield_prev[i]);
	      if (field_get_refcount (pfield_prev[i]) == 0)
		free_field (pfield_prev[i]);
	    }
	}
    }

  return SCM_UNSPECIFIED;
}


void
gucu_form_init_type ()
{
  field_tag = scm_make_smob_type ("field", sizeof (FIELD *));
  scm_set_smob_mark (field_tag, mark_field);
  scm_set_smob_free (field_tag, gc_free_field);
  scm_set_smob_print (field_tag, print_field);
  scm_set_smob_equalp (field_tag, equalp_field);
  scm_c_define_gsubr ("field?", 1, 0, 0, gucu_is_field_p);
  scm_c_define_gsubr ("%field-refcount", 1, 0, 0, gucu_field_refcount);


  form_tag = scm_make_smob_type ("form", sizeof (FORM *));
  scm_set_smob_mark (form_tag, mark_form);
  scm_set_smob_free (form_tag, gc_free_form);
  scm_set_smob_print (form_tag, print_form);
  scm_set_smob_equalp (form_tag, equalp_form);
  scm_c_define_gsubr ("form?", 1, 0, 0, gucu_is_form_p);

  scm_c_define_gsubr ("new-field", 6, 0, 0, gucu_new_field);
  scm_c_define_gsubr ("new-form", 1, 0, 0, gucu_new_form);
  scm_c_define_gsubr ("form-fields", 1, 0, 0, gucu_form_fields);
  scm_c_define_gsubr ("set-form-fields!", 2, 0, 0, gucu_set_form_fields_x);
}
