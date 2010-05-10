/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * Author:
 * Philip Van Hoof <philip@codeminded.be>
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libtracker-db/tracker-db.h>

#include <libtracker-data/tracker-data-manager.h>
#include <libtracker-data/tracker-data-query.h>
#include <libtracker-data/tracker-data-update.h>
#include <libtracker-data/tracker-sparql-query.h>

typedef struct _TestInfo TestInfo;

struct _TestInfo {
	const gchar *test_name;
	const gchar *data;
};

typedef struct _ChangeInfo ChangeInfo;

struct _ChangeInfo {
	const gchar *ontology;
	const gchar *update;
	const gchar *test_name;
	const gchar *ptr;
};

const TestInfo change_tests[] = {
	{ "change/test-1", "change/data-1" },
	{ "change/test-2", "change/data-2" },
	{ "change/test-3", "change/data-3" },
	{ "change/test-4", "change/data-4" },
	{ "change/test-5", "change/data-5" },
	{ NULL }
};

const ChangeInfo changes[] = {
	{ "99-example.ontology.v1", "99-example.queries.v1", NULL, NULL },
	{ "99-example.ontology.v2", "99-example.queries.v2", NULL, NULL },
	{ "99-example.ontology.v3", "99-example.queries.v3", NULL, NULL },
	{ "99-example.ontology.v4", "99-example.queries.v4", NULL, NULL },
	{ "99-example.ontology.v5", "99-example.queries.v5", "change/change-test-1", NULL },
	{ "99-example.ontology.v6", "99-example.queries.v6", "change/change-test-2", NULL },
	{ "99-example.ontology.v7", "99-example.queries.v7", "change/change-test-3", NULL },
	{ NULL }
};

static void
delete_db (gboolean del_journal)
{
	gchar *meta_db, *db_location;

	db_location = g_build_path (G_DIR_SEPARATOR_S, g_get_current_dir (), "tracker", NULL);
	meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "meta.db", NULL);
	g_unlink (meta_db);
	g_free (meta_db);

	if (del_journal) {
		meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "data", "tracker-store.journal", NULL);
		g_unlink (meta_db);
		g_free (meta_db);
	}

	meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "data", ".meta.isrunning", NULL);
	g_unlink (meta_db);
	g_free (meta_db);

	g_free (db_location);
}

static void
query_helper (const gchar *query_filename, const gchar *results_filename)
{
	GError *error = NULL;
	gchar *queries = NULL, *query;
	gchar *results = NULL;
	GString *test_results = NULL;

	g_file_get_contents (query_filename, &queries, NULL, &error);
	g_assert_no_error (error);

	g_file_get_contents (results_filename, &results, NULL, &error);
	g_assert_no_error (error);

	/* perform actual query */

	query = strtok (queries, "~");

	while (query) {
		TrackerDBResultSet *result_set;

		result_set = tracker_data_query_sparql (query, &error);
		g_assert_no_error (error);

		/* compare results with reference output */

		if (!test_results) {
			test_results = g_string_new ("");
		} else {
			g_string_append (test_results, "~\n");
		}

		if (result_set) {
			gboolean valid = TRUE;
			guint col_count;
			gint col;

			col_count = tracker_db_result_set_get_n_columns (result_set);

			while (valid) {
				for (col = 0; col < col_count; col++) {
					GValue value = { 0 };

					_tracker_db_result_set_get_value (result_set, col, &value);

					switch (G_VALUE_TYPE (&value)) {
					case G_TYPE_INT64:
						g_string_append_printf (test_results, "\"%" G_GINT64_FORMAT "\"", g_value_get_int64 (&value));
						break;
					case G_TYPE_DOUBLE:
						g_string_append_printf (test_results, "\"%f\"", g_value_get_double (&value));
						break;
					case G_TYPE_STRING:
						g_string_append_printf (test_results, "\"%s\"", g_value_get_string (&value));
						break;
					default:
						/* unbound variable */
						break;
					}

					if (col < col_count - 1) {
						g_string_append (test_results, "\t");
					}
				}

				g_string_append (test_results, "\n");

				valid = tracker_db_result_set_iter_next (result_set);
			}

			g_object_unref (result_set);
		}

		query = strtok (NULL, "~");
	}

	if (strcmp (results, test_results->str)) {
		/* print result difference */
		gchar *quoted_results;
		gchar *command_line;
		gchar *quoted_command_line;
		gchar *shell;
		gchar *diff;

		quoted_results = g_shell_quote (test_results->str);
		command_line = g_strdup_printf ("echo -n %s | diff -u %s -", quoted_results, results_filename);
		quoted_command_line = g_shell_quote (command_line);
		shell = g_strdup_printf ("sh -c %s", quoted_command_line);
		g_spawn_command_line_sync (shell, &diff, NULL, NULL, &error);
		g_assert_no_error (error);

		g_error ("%s", diff);

		g_free (quoted_results);
		g_free (command_line);
		g_free (quoted_command_line);
		g_free (shell);
		g_free (diff);
	}

	g_string_free (test_results, TRUE);
	g_free (results);
	g_free (queries);
}

static void
test_ontology_change (void)
{
	gchar *ontology_file;
	GFile *file2;
	gchar *prefix, *build_prefix;
	gchar *ontology_dir;
	guint i;
	GError *error = NULL;
	gchar *test_schemas[5] = { NULL, NULL, NULL, NULL, NULL };

	delete_db (TRUE);

	prefix = g_build_path (G_DIR_SEPARATOR_S, TOP_SRCDIR, "tests", "libtracker-data", NULL);
	build_prefix = g_build_path (G_DIR_SEPARATOR_S, TOP_BUILDDIR, "tests", "libtracker-data", NULL);

	test_schemas[0] = g_build_path (G_DIR_SEPARATOR_S, prefix, "ontologies", "20-dc", NULL);
	test_schemas[1] = g_build_path (G_DIR_SEPARATOR_S, prefix, "ontologies", "31-nao", NULL);
	test_schemas[2] = g_build_path (G_DIR_SEPARATOR_S, prefix, "ontologies", "90-tracker", NULL);
	test_schemas[3] = g_build_path (G_DIR_SEPARATOR_S, build_prefix, "change", "ontologies", "99-example", NULL);

	ontology_file = g_build_path (G_DIR_SEPARATOR_S, build_prefix, "change", "ontologies", "99-example.ontology", NULL);

	file2 = g_file_new_for_path (ontology_file);

	g_file_delete (file2, NULL, NULL);

	ontology_dir = g_build_path (G_DIR_SEPARATOR_S, build_prefix, "change", "ontologies", NULL);
	g_mkdir_with_parents (ontology_dir, 0777);
	g_free (ontology_dir);

	for (i = 0; changes[i].ontology; i++) {
		GFile *file1;
		gchar *queries = NULL;
		gchar *source = g_build_path (G_DIR_SEPARATOR_S, prefix, "change", "source", changes[i].ontology, NULL);
		gchar *update = g_build_path (G_DIR_SEPARATOR_S, prefix, "change", "updates", changes[i].update, NULL);
		gchar *from, *to;

		file1 = g_file_new_for_path (source);

		from = g_file_get_path (file1);
		to = g_file_get_path (file2);
		g_debug ("copy %s to %s", from, to);
		g_free (from);
		g_free (to);

		g_file_copy (file1, file2, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);

		g_assert_no_error (error);
		g_chmod (ontology_file, 0666);

		tracker_data_manager_init (0, (const gchar **) test_schemas,
		                           NULL, FALSE, NULL, NULL, NULL);

		if (g_file_get_contents (update, &queries, NULL, NULL)) {
			gchar *query = strtok (queries, "\n");
			while (query) {

				tracker_data_begin_db_transaction ();
				tracker_data_update_sparql (query, &error);
				tracker_data_commit_db_transaction ();

				g_assert_no_error (error);
				query = strtok (NULL, "\n");
			}
			g_free (queries);
		}

		g_free (update);
		g_free (source);
		g_object_unref (file1);


		if (changes[i].test_name) {
			gchar *query_filename;
			gchar *results_filename;
			gchar *test_prefix;

			test_prefix = g_build_filename (prefix, changes[i].test_name, NULL);
			query_filename = g_strconcat (test_prefix, ".rq", NULL);
			results_filename = g_strconcat (test_prefix, ".out", NULL);

			query_helper (query_filename, results_filename);

			g_free (test_prefix);
			g_free (query_filename);
			g_free (results_filename);
		}

		tracker_data_manager_shutdown ();
	}

	delete_db (FALSE);

	tracker_data_manager_init (0, (const gchar **) test_schemas,
	                           NULL, TRUE, NULL, NULL, NULL);

	for (i = 0; change_tests[i].test_name != NULL; i++) {
		gchar *query_filename;
		gchar *results_filename;
		gchar *test_prefix;

		test_prefix = g_build_filename (prefix, change_tests[i].test_name, NULL);
		query_filename = g_strconcat (test_prefix, ".rq", NULL);
		results_filename = g_strconcat (test_prefix, ".out", NULL);

		query_helper (query_filename, results_filename);

		g_free (test_prefix);
		g_free (query_filename);
		g_free (results_filename);
	}

	tracker_data_manager_shutdown ();

	g_file_delete (file2, NULL, NULL);

	g_object_unref (file2);
	g_free (test_schemas[0]);
	g_free (test_schemas[1]);
	g_free (test_schemas[2]);
	g_free (build_prefix);
	g_free (prefix);
}

int
main (int argc, char **argv)
{
	gint result;
	gchar *data_dir;

	g_type_init ();

	if (!g_thread_supported ()) {
		g_thread_init (NULL);
	}

	g_test_init (&argc, &argv, NULL);

	data_dir = g_build_filename (g_get_current_dir (), "test-cache", NULL);

	g_setenv ("XDG_DATA_HOME", data_dir, TRUE);
	g_setenv ("XDG_CACHE_HOME", data_dir, TRUE);
	g_setenv ("TRACKER_DB_SQL_DIR", TOP_SRCDIR "/data/db/", TRUE);
	g_setenv ("TRACKER_DB_ONTOLOGIES_DIR", TOP_SRCDIR "/data/ontologies/", TRUE);

	/* add test cases */

	g_test_add_func ("/libtracker-data/ontology-change", test_ontology_change);


	/* run tests */

	result = g_test_run ();

	/* clean up */
	g_print ("Removing temporary data\n");
	g_spawn_command_line_sync ("rm -R tracker/", NULL, NULL, NULL, NULL);
	g_spawn_command_line_sync ("rm -R test-cache/", NULL, NULL, NULL, NULL);

	g_free (data_dir);

	return result;
}
