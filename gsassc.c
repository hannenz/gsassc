#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <sass_interface.h>

static gchar *outfile = NULL;
static gchar *outdir = NULL;
static gchar *style = "nested";
static gboolean line_numbers = FALSE;
static gboolean source_map = FALSE;
static gchar *include_paths = NULL;
static gchar *watch = NULL;
static gboolean verbose = FALSE;

static GOptionEntry entries [] = {
	{	"output", 'o', 0, G_OPTION_ARG_FILENAME, &outfile, "Write to specified file", NULL},
	{	"style", 't', 0, G_OPTION_ARG_STRING, &style, "Output style. Can be nested, expanded, compact or compressed (Note: nested, compact and expanded are the same by now, waiting for libsass to implement them... ;))" , NULL},
	{	"line-numbers", 'l', 0, G_OPTION_ARG_NONE, &line_numbers, "Emit comments showing original line numbers.", NULL},
	{	"source-map", 'g', 0, G_OPTION_ARG_NONE, &source_map, "Emit source map.", NULL},
	{	"import-path", 'I', 0, G_OPTION_ARG_STRING, &include_paths, "Set Sass import path (colon delimited list of paths).", NULL},
	{	"watch", 'w', 0, G_OPTION_ARG_STRING, &watch, "Watch for changes. Give either infile:outfile or indir:outdir as argument", NULL},
	{	"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	{ NULL }
};


gint output(gint error_status, gchar* error_message, gchar* output_string, gchar* outfile) {
	if (error_status) {
		if (error_message) {
			g_print("%s", error_message);
		}
		else {
			g_print("An error occured; no error message available\n");
		}
		return 1;
	}
	else if (output_string) {
		if (verbose){
			g_print("[OK]\n");
		}
		if (outfile) {

			if (verbose){
				g_print("Writing to file: %s\n", outfile);
			}

			GError *error = NULL;
			if (!g_file_set_contents(outfile, output_string, -1, &error)) {
				g_error("Failed to write to output file %s: %s", outfile, error->message);
				g_error_free(error);
				return 1;
			}
		}
		else {
			g_print("%s\n", output_string);
		}

		return 0;
	}
	else {
		g_error("Unknown internal error.\n");
		return 2;
	}
}


gint compile_file(struct sass_options options, gchar* input_path, gchar* outfile) {
	gint ret;

	if (verbose){
		g_print("Compiling file: %s\n", input_path);
	}

	struct sass_file_context *context = sass_new_file_context();
	context->options = options;
	context->input_path = input_path;

	sass_compile_file(context);

	ret = output(context->error_status, context->error_message, context->output_string, outfile);

	sass_free_file_context(context);

	if (verbose) {
		g_print("Done.\n");
	}
	return ret;
}


gint compile_stdin(struct sass_options options, gchar* outfile) {

	GIOChannel *ch;
	GString *file, *line;
	GError *error = NULL;
	GIOStatus status;
	gint ret;

	struct sass_context *ctx;


	file = g_string_new(NULL);
	line = g_string_new(NULL);
	ch = g_io_channel_unix_new(1);

	do {
		error = NULL;
		status = g_io_channel_read_line_string(ch, line, NULL, &error);

		if (status == G_IO_STATUS_NORMAL) {
			file = g_string_append(file, line->str);
		}
		else if (status == G_IO_STATUS_ERROR) {
			g_error("Failed to read line from stdin: %s\n", error->message);
			g_error_free(error);

			error = NULL;
			status = g_io_channel_shutdown(ch, TRUE, &error);
			if (status == G_IO_STATUS_ERROR){
				g_error("Failed to shutdown io channel: %s\n", error->message);
				g_error_free(error);
			}
			return 1;
		}
	}
	while (status != G_IO_STATUS_EOF);

	ctx = sass_new_context();
	ctx->options = options;
	ctx->source_string = file->str;
	sass_compile(ctx);
	ret = output(ctx->error_status, ctx->error_message, ctx->output_string, outfile);

	sass_free_context(ctx);

	g_string_free(line, TRUE);
	g_string_free(file, TRUE);

	return ret;
}


gchar *change_suffix(const gchar *filename){

	gchar *dot, *tmp, *ret;

	tmp = g_strdup(filename);

	dot = g_strrstr(tmp, ".");

	if (dot != NULL) {
		*dot = '\0';
		ret = g_strdup_printf("%s.css", tmp);
		g_free(tmp);
		return ret;
	}
	else {
		return NULL;
	}
}

static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer udata) {

	struct sass_options options;
	gchar *file_path;
	
	options = *(struct sass_options*)udata;

	if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		file_path = g_file_get_path(file);

		if (verbose){
			g_print("Change detected in file: %s\n", file_path);
		}

		compile_file(options, file_path, outfile);
	}
}

static void on_directory_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer udata) {

	gchar *file_path, *filename, *watchdir;
	const gchar *name;
	struct sass_options options;
	GError *error;
	GDir *dir;
	GList *non_partials;
//	gint n;

	options = *(struct sass_options*)udata;

	if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {

		file_path = g_file_get_path(file);
		if (!g_file_test(file_path, G_FILE_TEST_EXISTS)){
			return;
		}

		filename = g_path_get_basename(file_path);


		if (g_str_has_suffix(file_path, ".scss")) {

			watchdir = g_file_get_path(g_file_get_parent(file));

			if (verbose){
				g_print("Change detected in file %s in dir: %s\n", file_path, watchdir);
			}

			if (g_str_has_prefix(filename, "_")) {

				error = NULL;
				dir = g_dir_open(watchdir, 0, &error);
				if (dir == NULL){
					g_error("Failed to read directory: %s\n", error->message);
					g_error_free(error);
					exit(1);
				}
				
				non_partials = NULL;
				while ((name = g_dir_read_name(dir)) != NULL) {

					if (g_str_has_suffix(name, ".scss") && !g_str_has_prefix(name, "_")){
						non_partials = g_list_append(non_partials, g_strdup(name));
					}
				}
				g_dir_close(dir);

//				n = g_list_length(non_partials);

				GList *ptr;
				for (ptr = non_partials; ptr != NULL; ptr = ptr->next){
					gchar *name, *basename, *outfile, *outpath, *inpath;
					
					name = ptr->data;
					inpath = g_build_filename(watchdir, name, NULL);

					basename = g_path_get_basename(name);
					outfile = change_suffix(basename);
					if (outfile){

						outpath = g_build_filename(outdir, outfile, NULL);
						compile_file(options, inpath, outpath);

						g_free(outpath);
					}

					g_free(outfile);
					g_free(basename);
					g_free(inpath);
					g_free(name);
				}
			}
			else {

				gchar *basename, *outfile, *outpath;
				basename = g_path_get_basename(file_path);
				outfile = change_suffix(basename);
				if (outfile){
					outpath = g_build_filename(outdir, outfile, NULL);
					compile_file(options, file_path, outpath);
					g_free(outpath);
				}
				g_free(outfile);
				g_free(basename);

			}

			// build out file name from infile name switch extension to .css and 
		}
	}
}



gint main (gint argc, gchar **argv) {

	GError *error = NULL;
	GOptionContext *context;
	GOptionGroup *group;
	GFileMonitor *monitor;

	g_type_init();

	/* Parse command line options */

	context = g_option_context_new("[source file]");
	g_option_context_set_summary(context, "Compile SASS (SCSS) files to CSS files.");
	g_option_context_add_main_entries(context, entries, NULL);

	group = g_option_group_new("My Options", "These are my options", "This is the help description for my options", NULL, NULL);
	g_option_context_add_group(context, group);
	if (!g_option_context_parse(context, &argc, &argv, &error)){
		g_print("Option parsing failed: %s\n", error->message);
		exit(1);
	}

	/* Setup libsass options */

	struct sass_options options;
	options.image_path = "images";
	options.include_paths = "";
	options.output_style = SASS_STYLE_NESTED;

	if (!g_strcmp0(style, "compressed")) {
		options.output_style = SASS_STYLE_COMPRESSED;
	}
	else if (!g_strcmp0(style, "nested")) {
		options.output_style = SASS_STYLE_NESTED;
	}
	else if (!g_strcmp0(style, "compact")) {
		options.output_style = SASS_STYLE_COMPACT;
	}
	else if (!g_strcmp0(style, "expanded")) {
		options.output_style = SASS_STYLE_EXPANDED;
	}
	else {
		g_print("Illegal output style: %s\n", style);
		exit(1);
	}

	options.include_paths = include_paths;
	if (line_numbers){
		options.source_comments = SASS_SOURCE_COMMENTS_DEFAULT;
	}
	if (source_map){
		options.source_comments = SASS_SOURCE_COMMENTS_MAP;
	}

	if (watch != NULL) {

		gchar **watchfiles;
		watchfiles = g_strsplit(watch, ":", 2);

		if (watchfiles[0] == NULL || watchfiles[1] == NULL){
			g_print("Invalid argument for watch. User --watch infile:outfile or --watch indir:outdir\n");
			exit(1);
		}

		if (g_file_test(watchfiles[0], G_FILE_TEST_IS_REGULAR)) {

			outfile = g_strdup(watchfiles[1]);

			error = NULL;
			if ((monitor = g_file_monitor_file(g_file_new_for_path(watchfiles[0]), 0, NULL, &error)) == NULL) {
				g_error("Failed to setup a file monitor: %s\n", error->message);
				g_error_free(error);
				exit(1);
			}

			if (verbose){
				g_print("%s is watching for changes in %s\n", argv[0], watchfiles[0]);
			}

			g_signal_connect(monitor, "changed", G_CALLBACK(on_file_changed), &options);
		}
		else if (g_file_test(watchfiles[0], G_FILE_TEST_IS_DIR) && g_file_test(watchfiles[1], G_FILE_TEST_IS_DIR)){

			outdir = g_strdup(watchfiles[1]);

			error = NULL;
			if ((monitor = g_file_monitor_directory(g_file_new_for_path(watchfiles[0]), 0, NULL, &error)) == NULL) {
				g_error("Failed to setup a file monitor: %s\n", error->message);
				g_error_free(error);
				exit(1);
			}

			if (verbose){
				g_print("%s is watching for changes in %s\n", argv[0], watchfiles[0]);
			}

			g_signal_connect(monitor, "changed", G_CALLBACK(on_directory_changed), &options);
		}
		else {
			g_print("Don't know what to watch. Use %s --watch infile:outfile or %s --watch indir:outdir. In the latter case make sure that both directories exist.\n", argv[0], argv[0]);
			exit(1);
		}

		g_strfreev(watchfiles);

		GMainLoop *loop;
		loop = g_main_loop_new(NULL, FALSE);
		g_main_loop_run(loop);
		return 0;
	}


	if (argc == 2) {
		compile_file(options, argv[1], outfile);
	}
	else if (argc == 1) {
		compile_stdin(options, outfile);
	}

	return 0;
}
