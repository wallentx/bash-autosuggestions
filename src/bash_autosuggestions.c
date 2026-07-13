#include <bash/builtins.h>
#include <bash/shell.h>
#include <bash/variables.h>
#include <bash/builtins/common.h>
#include <bash/array.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <locale.h>
#include <readline/history.h>
#include <readline/keymaps.h>
#include <readline/readline.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

extern int wcwidth(wchar_t wc);

#ifndef EXECUTION_SUCCESS
#define EXECUTION_SUCCESS 0
#endif

#ifndef EXECUTION_FAILURE
#define EXECUTION_FAILURE 1
#endif

#ifndef EX_USAGE
#define EX_USAGE 258
#endif

static int bas_installed;
static int bas_enabled = 1;
static int bas_in_redisplay;
static int bas_drawn_rows = -1;
static int bas_drawn_end_row;

static char *bas_suggestion;
static char *bas_suffix;
static char *bas_last_line;
static int bas_last_point = -1;
static int bas_last_end = -1;
static int bas_async_fd = -1;
static pid_t bas_async_pid = -1;
static char *bas_async_prefix;
static char *bas_async_output;
static size_t bas_async_output_len;

static rl_hook_func_t *bas_original_event_hook;
static rl_vintfunc_t *bas_original_prep_term_function;
static int bas_original_keyboard_timeout = -1;
static int bas_in_prep_terminal;
static int bas_accept(int count, int key);
static int bas_execute(int count, int key);
static int bas_newline(int count, int key);
static int bas_interrupt(int count, int key);
static int bas_clear_command(int count, int key);
static int bas_fetch_command(int count, int key);
static int bas_enable_command(int count, int key);
static int bas_disable_command(int count, int key);
static int bas_toggle_command(int count, int key);
static int bas_forward_char(int count, int key);
static int bas_end_of_line(int count, int key);
static int bas_forward_word(int count, int key);
static int bas_vi_forward_char(int count, int key);
static int bas_vi_forward_word(int count, int key);
static int bas_vi_end_word(int count, int key);
static int bas_vi_find_next_char(int count, int key);
static int bas_bracketed_paste_begin(int count, int key);
static void bas_clear_suggestion(void);
static int bas_fetch_and_set_suggestion(void);

static int bas_input_fd(void) {
  FILE *in = rl_instream ? rl_instream : stdin;
  return fileno(in);
}

static int bas_set_input_isig(int enabled, struct termios *saved) {
  int fd = bas_input_fd();
  if (fd < 0) {
    return 0;
  }

  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    return 0;
  }

  if (saved) {
    *saved = tio;
  }

  if (enabled) {
    tio.c_lflag |= ISIG;
  } else {
    tio.c_lflag &= ~ISIG;
  }
  return tcsetattr(fd, TCSANOW, &tio) == 0;
}

static void bas_restore_input_termios(const struct termios *saved) {
  int fd = bas_input_fd();
  if (fd >= 0 && saved) {
    tcsetattr(fd, TCSANOW, saved);
  }
}

struct bas_binding {
  const char *keyseq;
  rl_command_func_t *replacement;
  rl_command_func_t *fallback;
  const char *keymap;
};

static struct bas_binding bas_default_bindings[] = {
    {"\r", bas_newline, rl_newline, NULL},
    {"\n", bas_newline, rl_newline, NULL},
    {"\r", bas_newline, rl_newline, "vi-insert"},
    {"\n", bas_newline, rl_newline, "vi-insert"},
    {"\r", bas_newline, rl_newline, "vi-movement"},
    {"\n", bas_newline, rl_newline, "vi-movement"},
    {"\003", bas_interrupt, rl_abort, NULL},                 /* C-c */
    {"\003", bas_interrupt, rl_abort, "vi-insert"},
    {"\003", bas_interrupt, rl_abort, "vi-movement"},
    {"\006", bas_forward_char, rl_forward_char, NULL},       /* C-f */
    {"\033[C", bas_forward_char, rl_forward_char, NULL},     /* right */
    {"\033OC", bas_forward_char, rl_forward_char, NULL},     /* app right */
    {"\005", bas_end_of_line, rl_end_of_line, NULL},         /* C-e */
    {"\033[F", bas_end_of_line, rl_end_of_line, NULL},       /* end */
    {"\033OF", bas_end_of_line, rl_end_of_line, NULL},       /* app end */
    {"\033[4~", bas_end_of_line, rl_end_of_line, NULL},
    {"\033[8~", bas_end_of_line, rl_end_of_line, NULL},
    {"\033f", bas_forward_word, rl_forward_word, NULL},      /* M-f */
    {"l", bas_vi_forward_char, rl_forward_char, "vi-movement"},
    {" ", bas_vi_forward_char, rl_forward_char, "vi-movement"},
    {"w", bas_vi_forward_word, rl_vi_next_word, "vi-movement"},
    {"e", bas_vi_end_word, rl_vi_end_word, "vi-movement"},
    {"f", bas_vi_find_next_char, rl_vi_char_search, "vi-movement"},
    {"\033[200~", bas_bracketed_paste_begin, rl_bracketed_paste_begin, NULL},
};

static const char *bas_long_doc[] = {
    "bash_autosuggestions [command]\n",
    "Display zsh-autosuggestions-style ghost text in Bash using Readline.\n",
    "Run bash_autosuggestions with no arguments for help.\n",
    (char *)0,
};

static void bas_free(char **slot) {
  if (*slot) {
    free(*slot);
    *slot = NULL;
  }
}

static char *bas_xstrdup(const char *s) {
  if (!s) {
    s = "";
  }
  char *copy = strdup(s);
  if (!copy) {
    fprintf(stderr, "bash-autosuggestions: out of memory\n");
  }
  return copy;
}

static void bas_set_string(char **slot, const char *value) {
  char *copy = bas_xstrdup(value);
  if (!copy) {
    return;
  }
  bas_free(slot);
  *slot = copy;
}

static const char *bas_var2(const char *bash_name, const char *zsh_name) {
  char *value = get_string_value(bash_name);
  if (value && *value) {
    return value;
  }

  value = get_string_value(zsh_name);
  if (value && *value) {
    return value;
  }

  return NULL;
}

static char *bas_var_words(const char *name) {
  SHELL_VAR *var = find_variable(name);
  if (!var) {
    return NULL;
  }

  if (array_p(var)) {
    ARRAY *array = array_cell(var);
    if (!array || array_empty(array)) {
      return NULL;
    }
    return array_to_string(array, " ", 0);
  }

  char *value = get_variable_value(var);
  if (!value || !*value) {
    return NULL;
  }
  return bas_xstrdup(value);
}

static char *bas_var_words2(const char *bash_name, const char *zsh_name) {
  char *value = bas_var_words(bash_name);
  if (value && *value) {
    return value;
  }
  free(value);

  value = bas_var_words(zsh_name);
  if (value && *value) {
    return value;
  }
  free(value);
  return NULL;
}

static int bas_prefix_matches(const char *line, const char *prefix) {
  size_t n = strlen(prefix);
  return strncmp(line, prefix, n) == 0;
}

static int bas_strategy_name_is_safe(const char *name) {
  if (!name || !*name) {
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
    if (!isalnum(*p) && *p != '_') {
      return 0;
    }
  }
  return 1;
}

static char *bas_shell_quote(const char *s) {
  size_t len = 2;
  for (const char *p = s; p && *p; p++) {
    len += (*p == '\'') ? 4 : 1;
  }

  char *quoted = malloc(len + 1);
  if (!quoted) {
    return NULL;
  }

  char *out = quoted;
  *out++ = '\'';
  for (const char *p = s; p && *p; p++) {
    if (*p == '\'') {
      memcpy(out, "'\\''", 4);
      out += 4;
    } else {
      *out++ = *p;
    }
  }
  *out++ = '\'';
  *out = '\0';
  return quoted;
}

static int bas_history_ignored(const char *line) {
  const char *pattern =
      bas_var2("BASH_AUTOSUGGEST_HISTORY_IGNORE", "ZSH_AUTOSUGGEST_HISTORY_IGNORE");
  if (!pattern || !*pattern) {
    return 0;
  }
  return fnmatch(pattern, line, 0) == 0;
}

static int bas_completion_ignored(const char *line) {
  const char *pattern = bas_var2("BASH_AUTOSUGGEST_COMPLETION_IGNORE",
                                 "ZSH_AUTOSUGGEST_COMPLETION_IGNORE");
  if (!pattern || !*pattern) {
    return 0;
  }
  return fnmatch(pattern, line, 0) == 0;
}

static int bas_max_size_allows(const char *prefix) {
  const char *raw =
      bas_var2("BASH_AUTOSUGGEST_BUFFER_MAX_SIZE", "ZSH_AUTOSUGGEST_BUFFER_MAX_SIZE");
  if (!raw || !*raw) {
    return 1;
  }

  char *end = NULL;
  errno = 0;
  long max = strtol(raw, &end, 10);
  if (errno != 0 || end == raw || max < 0) {
    return 1;
  }

  return (long)strlen(prefix) <= max;
}

static char *bas_strategy_history(const char *prefix) {
  HIST_ENTRY **entries = history_list();
  if (!entries) {
    return NULL;
  }

  for (int i = history_length - 1; i >= 0; i--) {
    HIST_ENTRY *entry = entries[i];
    if (!entry || !entry->line) {
      continue;
    }
    if (bas_prefix_matches(entry->line, prefix) && !bas_history_ignored(entry->line)) {
      return bas_xstrdup(entry->line);
    }
  }

  return NULL;
}

static char *bas_strategy_match_prev_cmd(const char *prefix) {
  HIST_ENTRY **entries = history_list();
  if (!entries || history_length <= 0) {
    return NULL;
  }

  const char *prev_cmd = entries[history_length - 1] ? entries[history_length - 1]->line : NULL;
  if (!prev_cmd) {
    return bas_strategy_history(prefix);
  }

  int fallback_index = -1;
  int seen = 0;
  for (int i = history_length - 1; i >= 0 && seen < 200; i--) {
    HIST_ENTRY *entry = entries[i];
    if (!entry || !entry->line) {
      continue;
    }
    if (!bas_prefix_matches(entry->line, prefix) || bas_history_ignored(entry->line)) {
      continue;
    }

    if (fallback_index < 0) {
      fallback_index = i;
    }
    seen++;

    if (i > 0 && entries[i - 1] && entries[i - 1]->line &&
        strcmp(entries[i - 1]->line, prev_cmd) == 0) {
      return bas_xstrdup(entry->line);
    }
  }

  if (fallback_index >= 0 && entries[fallback_index] && entries[fallback_index]->line) {
    return bas_xstrdup(entries[fallback_index]->line);
  }

  return NULL;
}

static int bas_find_word_start(const char *line, int end) {
  int start = end;
  while (start > 0 && !isspace((unsigned char)line[start - 1])) {
    start--;
  }
  return start;
}

static void bas_free_matches(char **matches) {
  if (!matches) {
    return;
  }
  for (int i = 0; matches[i]; i++) {
    free(matches[i]);
  }
  free(matches);
}

static char *bas_strategy_completion(const char *prefix) {
  if (bas_completion_ignored(prefix)) {
    return NULL;
  }
  if (!rl_line_buffer || rl_end < 0) {
    return NULL;
  }

  int end = rl_end;
  int start = bas_find_word_start(rl_line_buffer, end);
  const char *word = rl_line_buffer + start;
  char *text = strndup(word, (size_t)(end - start));
  if (!text) {
    return NULL;
  }

  char **matches = NULL;
  if (rl_attempted_completion_function) {
    matches = rl_attempted_completion_function(text, start, end);
  }
  if (!matches) {
    rl_compentry_func_t *entry_function = rl_completion_entry_function;
    if (!entry_function) {
      entry_function = rl_filename_completion_function;
    }
    matches = rl_completion_matches(text, entry_function);
  }

  char *candidate = NULL;
  if (matches && matches[0]) {
    candidate = matches[1] ? matches[1] : matches[0];
  }

  char *result = NULL;
  if (candidate && strlen(candidate) > strlen(text)) {
    size_t before_len = (size_t)start;
    size_t candidate_len = strlen(candidate);
    result = malloc(before_len + candidate_len + 1);
    if (result) {
      memcpy(result, prefix, before_len);
      memcpy(result + before_len, candidate, candidate_len + 1);
    }
  }

  bas_free_matches(matches);
  free(text);
  return result;
}

static char *bas_strategy_custom(const char *strategy, const char *prefix) {
  if (!bas_strategy_name_is_safe(strategy)) {
    return NULL;
  }

  char *quoted_prefix = bas_shell_quote(prefix);
  if (!quoted_prefix) {
    return NULL;
  }

  const char *template =
      "_BASH_AUTOSUGGEST_CUSTOM_RESULT=\n"
      "suggestion=\n"
      "if declare -F _bash_autosuggest_strategy_%s >/dev/null 2>&1; then\n"
      "  _bash_autosuggest_strategy_%s %s\n"
      "  _BASH_AUTOSUGGEST_CUSTOM_RESULT=$suggestion\n"
      "elif declare -F _zsh_autosuggest_strategy_%s >/dev/null 2>&1; then\n"
      "  _zsh_autosuggest_strategy_%s %s\n"
      "  _BASH_AUTOSUGGEST_CUSTOM_RESULT=$suggestion\n"
      "fi\n"
      "unset _bash_autosuggest_custom_stdout\n";

  int needed = snprintf(NULL, 0, template, strategy, strategy, quoted_prefix, strategy, strategy,
                        quoted_prefix);
  if (needed < 0) {
    free(quoted_prefix);
    return NULL;
  }

  char *script = malloc((size_t)needed + 1);
  if (!script) {
    free(quoted_prefix);
    return NULL;
  }
  snprintf(script, (size_t)needed + 1, template, strategy, strategy, quoted_prefix, strategy,
           strategy, quoted_prefix);
  free(quoted_prefix);

  evalstring(script, "bash-autosuggestions", SEVAL_NOHIST | SEVAL_NOOPTIMIZE);

  char *value = get_string_value("_BASH_AUTOSUGGEST_CUSTOM_RESULT");
  char *result = (value && *value) ? bas_xstrdup(value) : NULL;

  char *cleanup = bas_xstrdup("unset _BASH_AUTOSUGGEST_CUSTOM_RESULT suggestion");
  if (cleanup) {
    evalstring(cleanup, "bash-autosuggestions", SEVAL_NOHIST | SEVAL_NOOPTIMIZE);
  }

  return result;
}

static char *bas_fetch_suggestion_for(const char *prefix) {
  if (!prefix || !*prefix || !bas_max_size_allows(prefix)) {
    return NULL;
  }

  char *strategies =
      bas_var_words2("BASH_AUTOSUGGEST_STRATEGY", "ZSH_AUTOSUGGEST_STRATEGY");
  if (!strategies) {
    strategies = bas_xstrdup("history");
    if (!strategies) {
      return NULL;
    }
  }

  char *saveptr = NULL;
  char *token = strtok_r(strategies, " \t\r\n,:", &saveptr);
  char *suggestion = NULL;
  while (token) {
    if (strcmp(token, "history") == 0) {
      suggestion = bas_strategy_history(prefix);
    } else if (strcmp(token, "match_prev_cmd") == 0) {
      suggestion = bas_strategy_match_prev_cmd(prefix);
    } else if (strcmp(token, "completion") == 0) {
      suggestion = bas_strategy_completion(prefix);
    } else {
      suggestion = bas_strategy_custom(token, prefix);
    }

    if (suggestion && bas_prefix_matches(suggestion, prefix)) {
      break;
    }

    free(suggestion);
    suggestion = NULL;
    token = strtok_r(NULL, " \t\r\n,:", &saveptr);
  }

  free(strategies);
  return suggestion;
}

static int bas_string_equals_trimmed(const char *value, const char *word) {
  if (!value || !word) {
    return 0;
  }

  while (value && isspace((unsigned char)*value)) {
    value++;
  }

  const char *end = value + strlen(value);
  while (end > value && isspace((unsigned char)end[-1])) {
    end--;
  }

  size_t len = (size_t)(end - value);
  if (len != strlen(word)) {
    return 0;
  }

  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char)value[i]) != tolower((unsigned char)word[i])) {
      return 0;
    }
  }
  return 1;
}

static int bas_async_value_is_false(const char *value) {
  return bas_string_equals_trimmed(value, "0") ||
         bas_string_equals_trimmed(value, "no") ||
         bas_string_equals_trimmed(value, "false") ||
         bas_string_equals_trimmed(value, "off");
}

static int bas_prompt_has_visible_newline(void) {
  const char *prompt = rl_display_prompt ? rl_display_prompt : rl_prompt;
  int hidden = 0;

  for (size_t i = 0; prompt && prompt[i]; i++) {
    unsigned char c = (unsigned char)prompt[i];
    if (c == '\001') {
      hidden = 1;
      continue;
    }
    if (c == '\002') {
      hidden = 0;
      continue;
    }
    if (hidden) {
      continue;
    }
    if (c == '\n' || c == '\r') {
      return 1;
    }
  }

  return 0;
}

static int bas_async_enabled(void) {
  const char *value = bas_var2("BASH_AUTOSUGGEST_USE_ASYNC",
                               "ZSH_AUTOSUGGEST_USE_ASYNC");
  if (!value) {
    return 1;
  }
  if (bas_async_value_is_false(value)) {
    return 0;
  }
  if (bas_string_equals_trimmed(value, "auto")) {
    return !bas_prompt_has_visible_newline();
  }
  return 1;
}

static void bas_async_reset_state(void) {
  if (bas_async_fd >= 0) {
    close(bas_async_fd);
    bas_async_fd = -1;
  }
  if (bas_async_pid > 0) {
    kill(bas_async_pid, SIGTERM);
    waitpid(bas_async_pid, NULL, WNOHANG);
    bas_async_pid = -1;
  }
  bas_free(&bas_async_prefix);
  bas_free(&bas_async_output);
  bas_async_output_len = 0;
}

static int bas_async_append(const char *buf, size_t len) {
  char *next = realloc(bas_async_output, bas_async_output_len + len + 1);
  if (!next) {
    return 0;
  }
  bas_async_output = next;
  memcpy(bas_async_output + bas_async_output_len, buf, len);
  bas_async_output_len += len;
  bas_async_output[bas_async_output_len] = '\0';
  return 1;
}

static int bas_apply_suggestion(const char *suggestion) {
  if (!rl_line_buffer || rl_end < 0 || !suggestion || !*suggestion) {
    if (bas_suggestion || bas_suffix) {
      bas_clear_suggestion();
      return 1;
    }
    return 0;
  }

  char *prefix = strndup(rl_line_buffer, (size_t)rl_end);
  if (!prefix) {
    return 0;
  }

  if (!*prefix) {
    free(prefix);
    if (bas_suggestion || bas_suffix) {
      bas_clear_suggestion();
      return 1;
    }
    return 0;
  }

  if (!bas_max_size_allows(prefix)) {
    free(prefix);
    if (bas_suggestion || bas_suffix) {
      bas_clear_suggestion();
      return 1;
    }
    return 0;
  }

  if (!bas_prefix_matches(suggestion, prefix)) {
    free(prefix);
    if (bas_suggestion || bas_suffix) {
      bas_clear_suggestion();
      return 1;
    }
    return 0;
  }

  const char *suffix = suggestion + strlen(prefix);
  int changed = !bas_suggestion || strcmp(bas_suggestion, suggestion) != 0 ||
                !bas_suffix || strcmp(bas_suffix, suffix) != 0;
  bas_set_string(&bas_suggestion, suggestion);
  bas_set_string(&bas_suffix, suffix);
  if (!*bas_suffix) {
    bas_free(&bas_suffix);
  }
  free(prefix);
  return changed;
}

static int bas_async_request_current(void) {
  if (!rl_line_buffer || rl_end <= 0 || !bas_max_size_allows(rl_line_buffer)) {
    bas_async_reset_state();
    if (bas_suggestion || bas_suffix) {
      bas_clear_suggestion();
      return 1;
    }
    return 0;
  }

  char *prefix = strndup(rl_line_buffer, (size_t)rl_end);
  if (!prefix) {
    return 0;
  }

  bas_async_reset_state();

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    free(prefix);
    return bas_fetch_and_set_suggestion();
  }

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) {
        close(devnull);
      }
    }
    char *suggestion = bas_fetch_suggestion_for(prefix);
    if (suggestion && *suggestion) {
      ssize_t ignored = write(pipefd[1], suggestion, strlen(suggestion));
      (void)ignored;
    }
    free(suggestion);
    close(pipefd[1]);
    _exit(0);
  }

  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    free(prefix);
    return bas_fetch_and_set_suggestion();
  }

  fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);
  bas_async_fd = pipefd[0];
  bas_async_pid = pid;
  bas_async_prefix = prefix;
  bas_clear_suggestion();
  return 1;
}

static int bas_async_poll(void) {
  if (bas_async_fd < 0) {
    return 0;
  }

  int changed = 0;
  char buf[512];
  for (;;) {
    ssize_t n = read(bas_async_fd, buf, sizeof(buf));
    if (n > 0) {
      bas_async_append(buf, (size_t)n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return changed;
    }
    break;
  }

  close(bas_async_fd);
  bas_async_fd = -1;
  if (bas_async_pid > 0) {
    waitpid(bas_async_pid, NULL, WNOHANG);
    bas_async_pid = -1;
  }

  int prefix_still_current = 0;
  if (rl_line_buffer && rl_end >= 0 && bas_async_prefix) {
    char *current_prefix = strndup(rl_line_buffer, (size_t)rl_end);
    if (current_prefix) {
      prefix_still_current = strcmp(current_prefix, bas_async_prefix) == 0;
      free(current_prefix);
    }
  }

  if (prefix_still_current) {
    changed |= bas_apply_suggestion(bas_async_output);
  } else if (bas_suggestion || bas_suffix) {
    bas_clear_suggestion();
    changed = 1;
  }
  bas_free(&bas_async_prefix);
  bas_free(&bas_async_output);
  bas_async_output_len = 0;
  return changed;
}

static int bas_update_existing_suggestion(void) {
  if (!rl_line_buffer || rl_end < 0 || !bas_suggestion) {
    int changed = bas_suggestion != NULL || bas_suffix != NULL;
    bas_free(&bas_suggestion);
    bas_free(&bas_suffix);
    return changed;
  }

  char *prefix = strndup(rl_line_buffer, (size_t)rl_end);
  if (!prefix) {
    return 0;
  }

  if (*prefix && bas_prefix_matches(bas_suggestion, prefix)) {
    const char *suffix = bas_suggestion + strlen(prefix);
    int changed = !bas_suffix || strcmp(bas_suffix, suffix) != 0;
    bas_set_string(&bas_suffix, suffix);
    if (!*bas_suffix) {
      bas_free(&bas_suffix);
    }
    free(prefix);
    return changed;
  }

  free(prefix);
  bas_clear_suggestion();
  return 1;
}

static int bas_fetch_and_set_suggestion(void) {
  if (!rl_line_buffer || rl_end < 0) {
    bas_free(&bas_suggestion);
    bas_free(&bas_suffix);
    return 1;
  }

  char *prefix = strndup(rl_line_buffer, (size_t)rl_end);
  if (!prefix) {
    return 0;
  }

  struct termios saved_tio;
  int saved_terminal = bas_set_input_isig(1, &saved_tio);
  char *suggestion = bas_fetch_suggestion_for(prefix);
  if (saved_terminal) {
    bas_restore_input_termios(&saved_tio);
  }
  if (!suggestion || !bas_prefix_matches(suggestion, prefix)) {
    free(prefix);
    free(suggestion);
    if (bas_suggestion || bas_suffix) {
      bas_free(&bas_suggestion);
      bas_free(&bas_suffix);
      return 1;
    }
    return 0;
  }

  const char *suffix = suggestion + strlen(prefix);
  int changed = !bas_suggestion || strcmp(bas_suggestion, suggestion) != 0 ||
                !bas_suffix || strcmp(bas_suffix, suffix) != 0;

  bas_set_string(&bas_suggestion, suggestion);
  bas_set_string(&bas_suffix, suffix);
  free(prefix);
  free(suggestion);
  return changed;
}

static int bas_update_suggestion(void) {
  if (!bas_enabled) {
    return bas_update_existing_suggestion();
  }
  if (bas_async_enabled()) {
    return bas_async_request_current();
  }
  bas_async_reset_state();
  return bas_fetch_and_set_suggestion();
}

static int bas_line_state_changed(void) {
  if (!rl_line_buffer) {
    return 0;
  }

  int changed = bas_last_point != rl_point || bas_last_end != rl_end ||
                !bas_last_line || strcmp(bas_last_line, rl_line_buffer) != 0;

  if (changed) {
    bas_last_point = rl_point;
    bas_last_end = rl_end;
    bas_set_string(&bas_last_line, rl_line_buffer);
  }

  return changed;
}

static int bas_style_color(const char *value, int background, char *out, size_t out_len) {
  if (!value || !*value) {
    return 0;
  }

  if (value[0] == '#') {
    unsigned int r, g, b;
    if (sscanf(value + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
      snprintf(out, out_len, "%s;2;%u;%u;%u", background ? "48" : "38", r, g, b);
      return 1;
    }
  }

  char *end = NULL;
  errno = 0;
  long idx = strtol(value, &end, 10);
  if (errno == 0 && end != value && *end == '\0' && idx >= 0 && idx <= 255) {
    snprintf(out, out_len, "%s;5;%ld", background ? "48" : "38", idx);
    return 1;
  }

  static const struct {
    const char *name;
    int idx;
  } colors[] = {
      {"black", 0},   {"red", 1},     {"green", 2}, {"yellow", 3},
      {"blue", 4},    {"magenta", 5}, {"cyan", 6},  {"white", 7},
      {"grey", 8},    {"gray", 8},
  };

  for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
    if (strcasecmp(value, colors[i].name) == 0) {
      snprintf(out, out_len, "%s;5;%d", background ? "48" : "38", colors[i].idx);
      return 1;
    }
  }

  return 0;
}

static void bas_style_sequence(char *out, size_t out_len) {
  const char *style =
      bas_var2("BASH_AUTOSUGGEST_HIGHLIGHT_STYLE", "ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE");
  if (!style || !*style) {
    style = "fg=8";
  }

  char params[128] = "";
  char *copy = bas_xstrdup(style);
  if (!copy) {
    snprintf(out, out_len, "\033[38;5;8m");
    return;
  }

  char *saveptr = NULL;
  char *token = strtok_r(copy, ",", &saveptr);
  while (token) {
    while (*token && isspace((unsigned char)*token)) {
      token++;
    }

    char part[64] = "";
    if (strncmp(token, "fg=", 3) == 0) {
      bas_style_color(token + 3, 0, part, sizeof(part));
    } else if (strncmp(token, "bg=", 3) == 0) {
      bas_style_color(token + 3, 1, part, sizeof(part));
    } else if (strcmp(token, "bold") == 0) {
      snprintf(part, sizeof(part), "1");
    } else if (strcmp(token, "underline") == 0) {
      snprintf(part, sizeof(part), "4");
    } else if (strcmp(token, "standout") == 0) {
      snprintf(part, sizeof(part), "7");
    }

    if (*part) {
      if (*params) {
        strncat(params, ";", sizeof(params) - strlen(params) - 1);
      }
      strncat(params, part, sizeof(params) - strlen(params) - 1);
    }

    token = strtok_r(NULL, ",", &saveptr);
  }

  free(copy);

  if (!*params) {
    snprintf(params, sizeof(params), "38;5;8");
  }
  snprintf(out, out_len, "\033[%sm", params);
}

static int bas_suffix_is_drawable(const char *suffix) {
  if (!suffix || !*suffix) {
    return 0;
  }
  return strchr(suffix, '\r') == NULL;
}

static int bas_in_vi_movement(void) {
  return rl_get_keymap() == vi_movement_keymap;
}

static int bas_at_accept_position(void) {
  if (!rl_line_buffer || rl_end <= 0) {
    return rl_point == rl_end;
  }
  return rl_point == rl_end || (bas_in_vi_movement() && rl_point == rl_end - 1);
}

static int bas_screen_columns(void) {
  int rows = 0;
  int cols = 0;
  rl_get_screen_size(&rows, &cols);
  return cols > 0 ? cols : 80;
}

static int bas_wcwidth_or_zero(wchar_t wc) {
  int width = wcwidth(wc);
  return width > 0 ? width : 0;
}

static size_t bas_char_cells(const char *s, size_t remaining, int *cells) {
  *cells = 0;
  if (!s || remaining == 0 || !*s) {
    return 0;
  }

  unsigned char c = (unsigned char)s[0];
  if (c < 0x80) {
    if (c >= 0x20 && c != 0x7f) {
      *cells = 1;
    }
    return 1;
  }

  mbstate_t state;
  memset(&state, 0, sizeof(state));
  wchar_t wc;
  size_t used = mbrtowc(&wc, s, remaining, &state);
  if (used == (size_t)-1 || used == (size_t)-2 || used == 0) {
    *cells = 1;
    return 1;
  }

  *cells = bas_wcwidth_or_zero(wc);
  return used;
}

static int bas_advance_cells(int *col, int screen_cols, int cells) {
  int rows = 0;
  while (cells-- > 0) {
    (*col)++;
    if (screen_cols > 0 && *col >= screen_cols) {
      *col = 0;
      rows++;
    }
  }
  return rows;
}

static int bas_advance_cell(int *col, int screen_cols, unsigned char c) {
  if (c == '\n' || c == '\r') {
    *col = 0;
    return 1;
  }

  if (c == '\b') {
    if (*col > 0) {
      (*col)--;
    }
    return 0;
  }

  if (c == '\t') {
    int width = 8 - (*col % 8);
    return bas_advance_cells(col, screen_cols, width);
  } else if (c >= 0x20 && c != 0x7f) {
    return bas_advance_cells(col, screen_cols, 1);
  }
  return 0;
}

static void bas_prompt_position(int *row, int *col) {
  const char *prompt = rl_prompt ? rl_prompt : rl_display_prompt;
  int hidden = 0;
  int screen_cols = bas_screen_columns();
  size_t prompt_len = prompt ? strlen(prompt) : 0;

  *row = 0;
  *col = 0;

  for (size_t i = 0; prompt && i < prompt_len;) {
    unsigned char c = (unsigned char)prompt[i];
    if (c == '\001') {
      hidden = 1;
      i++;
      continue;
    }
    if (c == '\002') {
      hidden = 0;
      i++;
      continue;
    }
    if (hidden) {
      i++;
      continue;
    }
    if (c == '\033' && prompt[i + 1] == '[') {
      i += 2;
      while (prompt[i] && !((unsigned char)prompt[i] >= 0x40 &&
                            (unsigned char)prompt[i] <= 0x7e)) {
        i++;
      }
      if (prompt[i]) {
        i++;
      }
      continue;
    }
    if (c == '\n' || c == '\r' || c == '\b' || c == '\t') {
      *row += bas_advance_cell(col, screen_cols, c);
      i++;
      continue;
    }
    int cells = 0;
    size_t used = bas_char_cells(prompt + i, prompt_len - i, &cells);
    if (used == 0) {
      break;
    }
    *row += bas_advance_cells(col, screen_cols, cells);
    i += used;
  }
}

static void bas_line_position(int end, int *row, int *col) {
  bas_prompt_position(row, col);
  int screen_cols = bas_screen_columns();

  for (int i = 0; rl_line_buffer && i < end;) {
    unsigned char c = (unsigned char)rl_line_buffer[i];
    if (c == '\n' || c == '\r' || c == '\b' || c == '\t') {
      *row += bas_advance_cell(col, screen_cols, c);
      i++;
      continue;
    }
    int cells = 0;
    size_t used = bas_char_cells(rl_line_buffer + i, (size_t)(end - i), &cells);
    if (used == 0) {
      break;
    }
    *row += bas_advance_cells(col, screen_cols, cells);
    i += (int)used;
  }
}

static int bas_line_end_column(void) {
  int row = 0;
  int col = 0;
  bas_line_position(rl_end, &row, &col);
  return col;
}

static int bas_suffix_rows(const char *suffix) {
  int col = bas_line_end_column();
  int rows = 0;
  int screen_cols = bas_screen_columns();
  size_t suffix_len = suffix ? strlen(suffix) : 0;

  for (size_t i = 0; suffix && i < suffix_len;) {
    unsigned char c = (unsigned char)suffix[i];
    if (c == '\n' || c == '\r' || c == '\b' || c == '\t') {
      rows += bas_advance_cell(&col, screen_cols, c);
      i++;
      continue;
    }
    int cells = 0;
    size_t used = bas_char_cells(suffix + i, suffix_len - i, &cells);
    if (used == 0) {
      break;
    }
    rows += bas_advance_cells(&col, screen_cols, cells);
    i += used;
  }

  return rows;
}

static void bas_move_cursor(FILE *out, int from_row, int to_row, int to_col) {
  if (to_row < from_row) {
    fprintf(out, "\033[%dA", from_row - to_row);
  } else if (to_row > from_row) {
    fprintf(out, "\033[%dB", to_row - from_row);
  }
  fprintf(out, "\033[%dG", to_col + 1);
}

static void bas_clear_drawn_suggestion(FILE *out) {
  if (bas_drawn_rows < 0) {
    return;
  }

  int cursor_row = 0;
  int cursor_col = 0;
  int clear_row = 0;
  int clear_col = 0;
  bas_line_position(rl_point, &cursor_row, &cursor_col);
  bas_line_position(rl_end, &clear_row, &clear_col);
  if (clear_row <= bas_drawn_end_row) {
    int clear_rows = bas_drawn_end_row - clear_row;
    bas_move_cursor(out, cursor_row, clear_row, clear_col);
    for (int row = 0; row <= clear_rows; row++) {
      fputs("\033[K", out);
      if (row < clear_rows) {
        fputs("\033[B\r", out);
      }
    }
    bas_move_cursor(out, clear_row + clear_rows, cursor_row, cursor_col);
  }
  bas_drawn_rows = -1;
}

static void bas_draw_suggestion(void) {
  int drawable = bas_suffix_is_drawable(bas_suffix) && rl_point <= rl_end;
  if (!drawable && bas_drawn_rows < 0) {
    return;
  }

  FILE *out = rl_outstream ? rl_outstream : stdout;
  bas_clear_drawn_suggestion(out);
  if (!drawable) {
    fflush(out);
    return;
  }

  char style[160];
  bas_style_sequence(style, sizeof(style));
  int cursor_row = 0;
  int cursor_col = 0;
  int suggestion_row = 0;
  int suggestion_col = 0;
  bas_line_position(rl_point, &cursor_row, &cursor_col);
  bas_line_position(rl_end, &suggestion_row, &suggestion_col);
  bas_drawn_rows = bas_suffix_rows(bas_suffix);
  bas_drawn_end_row = suggestion_row + bas_drawn_rows;
  bas_move_cursor(out, cursor_row, suggestion_row, suggestion_col);
  fputs(style, out);
  fputs(bas_suffix, out);
  fputs("\033[0m", out);
  bas_move_cursor(out, bas_drawn_end_row, cursor_row, cursor_col);
  fflush(out);
}

static void bas_redisplay(void) {
  if (bas_in_redisplay) {
    return;
  }

  bas_in_redisplay = 1;
  if (bas_line_state_changed()) {
    bas_update_suggestion();
  }
  rl_redisplay();
  bas_draw_suggestion();
  bas_in_redisplay = 0;
}

static int bas_event_hook(void) {
  int changed = 0;
  int state_changed = 0;
  if (rl_pending_signal()) {
    bas_drawn_rows = -1;
    bas_async_reset_state();
    bas_clear_suggestion();
    rl_check_signals();
    return 0;
  }
  if (bas_original_event_hook) {
    changed |= bas_original_event_hook();
  }

  changed |= bas_async_poll();

  state_changed = bas_line_state_changed();
  if (state_changed) {
    changed |= bas_update_suggestion();
  }

  if (changed || state_changed) {
    bas_draw_suggestion();
  }
  return 0;
}

static void bas_force_refresh(void) {
  bas_line_state_changed();
  bas_update_suggestion();
  bas_redisplay();
}

static void bas_clear_suggestion(void) {
  bas_free(&bas_suggestion);
  bas_free(&bas_suffix);
}

static void bas_update_suggestion_for_accept(void) {
  if (!bas_enabled || !rl_line_buffer || rl_end <= 0 || !bas_at_accept_position()) {
    return;
  }

  if (bas_line_state_changed() || !bas_suggestion || !bas_suffix || !*bas_suffix) {
    bas_async_reset_state();
    bas_fetch_and_set_suggestion();
  }
}

static int bas_insert_suffix(void) {
  bas_update_suggestion_for_accept();
  if (!bas_suggestion || !bas_suffix || !*bas_suffix || !bas_at_accept_position()) {
    return 0;
  }

  rl_begin_undo_group();
  rl_replace_line(bas_suggestion, 1);
  rl_point = bas_in_vi_movement() && rl_end > 0 ? rl_end - 1 : rl_end;
  rl_end_undo_group();
  bas_clear_suggestion();
  bas_force_refresh();
  return 1;
}

static int bas_partial_accept_with(rl_command_func_t *move_func, int count, int key) {
  bas_update_suggestion_for_accept();
  if (!bas_suggestion || !bas_suffix || !*bas_suffix || !bas_at_accept_position()) {
    return 0;
  }

  char *full = bas_xstrdup(bas_suggestion);
  char *original = rl_line_buffer ? strndup(rl_line_buffer, (size_t)rl_end) : NULL;
  if (!full || !original) {
    free(full);
    free(original);
    return 0;
  }

  int original_end = rl_end;
  int original_point = rl_point;
  int vi = bas_in_vi_movement();

  rl_begin_undo_group();
  rl_replace_line(full, 1);
  rl_point = original_point;
  move_func(count, key);

  int cursor_loc = vi ? rl_point + 1 : rl_point;
  if (cursor_loc > rl_end) {
    cursor_loc = rl_end;
  }

  if (cursor_loc > original_end) {
    char *accepted = strndup(full, (size_t)cursor_loc);
    char *tail = bas_xstrdup(full + cursor_loc);
    if (!accepted || !tail) {
      free(accepted);
      free(tail);
      rl_replace_line(original, 1);
      rl_point = original_point;
      rl_end_undo_group();
      free(full);
      free(original);
      return 0;
    }
    rl_replace_line(accepted, 1);
    rl_point = vi && rl_end > 0 ? rl_end - 1 : rl_end;
    bas_set_string(&bas_suggestion, full);
    bas_set_string(&bas_suffix, tail);
    free(accepted);
    free(tail);
  } else {
    rl_replace_line(original, 1);
    rl_point = original_point;
  }

  rl_end_undo_group();
  free(full);
  free(original);
  bas_force_refresh();
  return 1;
}

static Keymap bas_keymap_for_binding(const struct bas_binding *binding) {
  if (!binding->keymap) {
    return NULL;
  }
  if (strcmp(binding->keymap, "vi-movement") == 0) {
    return vi_movement_keymap;
  }
  return rl_get_keymap_by_name(binding->keymap);
}

static int bas_bind_keyseq(const struct bas_binding *binding, rl_command_func_t *function) {
  Keymap map = bas_keymap_for_binding(binding);
  if (map) {
    return rl_bind_keyseq_in_map(binding->keyseq, function, map);
  }
  if (binding->keymap) {
    return -1;
  }
  return rl_bind_keyseq(binding->keyseq, function);
}

static int bas_restore_keyseq(const struct bas_binding *binding) {
  return bas_bind_keyseq(binding, binding->fallback);
}

static int bas_bind_readline_keyseq(const char *keyseq, const char *function_name) {
  if (!keyseq || !*keyseq || !function_name || !*function_name) {
    return -1;
  }

  size_t len = strlen(keyseq) + strlen(function_name) + 5;
  char *line = malloc(len);
  if (!line) {
    return -1;
  }
  snprintf(line, len, "\"%s\": %s", keyseq, function_name);
  int result = rl_parse_and_bind(line);
  free(line);
  return result;
}

static void bas_bind_keyseq_words(char *words, const char *function_name) {
  if (!words) {
    return;
  }

  char *saveptr = NULL;
  char *token = strtok_r(words, " \t\r\n,", &saveptr);
  while (token) {
    bas_bind_readline_keyseq(token, function_name);
    token = strtok_r(NULL, " \t\r\n,", &saveptr);
  }
}

static void bas_bind_configured_keyseqs(void) {
  char *words = bas_var_words2("BASH_AUTOSUGGEST_ACCEPT_KEYSEQS",
                               "ZSH_AUTOSUGGEST_ACCEPT_KEYSEQS");
  bas_bind_keyseq_words(words, "autosuggest-accept");
  free(words);

  words = bas_var_words2("BASH_AUTOSUGGEST_EXECUTE_KEYSEQS",
                         "ZSH_AUTOSUGGEST_EXECUTE_KEYSEQS");
  bas_bind_keyseq_words(words, "autosuggest-execute");
  free(words);

  words = bas_var_words2("BASH_AUTOSUGGEST_CLEAR_KEYSEQS",
                         "ZSH_AUTOSUGGEST_CLEAR_KEYSEQS");
  bas_bind_keyseq_words(words, "autosuggest-clear");
  free(words);

  words = bas_var_words2("BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS",
                         "ZSH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS");
  bas_bind_keyseq_words(words, "autosuggest-forward-word");
  free(words);
}

static void bas_set_default_async(void) {
}

static int bas_call_or_partial(rl_command_func_t *move_func, int count, int key) {
  if (bas_partial_accept_with(move_func, count, key)) {
    return 0;
  }
  if (move_func) {
    return move_func(count, key);
  }
  return 0;
}

static int bas_call_or_accept(rl_command_func_t *move_func, int count, int key) {
  if (bas_insert_suffix()) {
    return 0;
  }
  if (move_func) {
    return move_func(count, key);
  }
  return 0;
}

static int bas_accept(int count, int key) {
  (void)count;
  (void)key;
  bas_insert_suffix();
  return 0;
}

static int bas_execute(int count, int key) {
  (void)count;
  bas_insert_suffix();
  return rl_newline(1, key);
}

static int bas_newline(int count, int key) {
  FILE *out = rl_outstream ? rl_outstream : stdout;
  bas_clear_drawn_suggestion(out);
  bas_clear_suggestion();
  fflush(out);
  return rl_newline(count, key);
}

static int bas_clear_command(int count, int key) {
  (void)count;
  (void)key;
  bas_clear_suggestion();
  bas_redisplay();
  return 0;
}

static int bas_fetch_command(int count, int key) {
  (void)count;
  (void)key;
  bas_line_state_changed();
  if (bas_async_enabled()) {
    bas_async_request_current();
  } else {
    bas_async_reset_state();
    bas_fetch_and_set_suggestion();
  }
  bas_redisplay();
  return 0;
}

static int bas_enable_command(int count, int key) {
  (void)count;
  (void)key;
  bas_enabled = 1;
  bas_force_refresh();
  return 0;
}

static int bas_disable_command(int count, int key) {
  (void)count;
  (void)key;
  bas_enabled = 0;
  bas_clear_suggestion();
  bas_redisplay();
  return 0;
}

static int bas_toggle_command(int count, int key) {
  (void)count;
  (void)key;
  bas_enabled = !bas_enabled;
  if (bas_enabled) {
    bas_force_refresh();
  } else {
    bas_clear_suggestion();
    bas_redisplay();
  }
  return 0;
}

static int bas_forward_char(int count, int key) {
  return bas_call_or_accept(rl_forward_char, count, key);
}

static int bas_end_of_line(int count, int key) {
  return bas_call_or_accept(rl_end_of_line, count, key);
}

static int bas_forward_word(int count, int key) {
  return bas_call_or_partial(rl_forward_word, count, key);
}

static int bas_vi_forward_char(int count, int key) {
  return bas_call_or_accept(rl_forward_char, count, key);
}

static int bas_vi_forward_word(int count, int key) {
  return bas_call_or_partial(rl_vi_next_word, count, key);
}

static int bas_vi_end_word(int count, int key) {
  return bas_call_or_partial(rl_vi_end_word, count, key);
}

static int bas_vi_find_next_char(int count, int key) {
  return bas_call_or_partial(rl_vi_char_search, count, key);
}

static int bas_bracketed_paste_begin(int count, int key) {
  bas_clear_suggestion();
  int result = rl_bracketed_paste_begin(count, key);
  bas_force_refresh();
  return result;
}

static void bas_add_readline_functions(void) {
  rl_add_defun("autosuggest-accept", bas_accept, -1);
  rl_add_defun("autosuggest-execute", bas_execute, -1);
  rl_add_defun("autosuggest-clear", bas_clear_command, -1);
  rl_add_defun("autosuggest-fetch", bas_fetch_command, -1);
  rl_add_defun("autosuggest-enable", bas_enable_command, -1);
  rl_add_defun("autosuggest-disable", bas_disable_command, -1);
  rl_add_defun("autosuggest-toggle", bas_toggle_command, -1);
  rl_add_defun("autosuggest-forward-char", bas_forward_char, -1);
  rl_add_defun("autosuggest-end-of-line", bas_end_of_line, -1);
  rl_add_defun("autosuggest-forward-word", bas_forward_word, -1);
}

static void bas_bind_defaults(void) {
  for (size_t i = 0; i < sizeof(bas_default_bindings) / sizeof(bas_default_bindings[0]); i++) {
    bas_bind_keyseq(&bas_default_bindings[i], bas_default_bindings[i].replacement);
  }
  bas_bind_configured_keyseqs();
}

static void bas_restore_defaults(void) {
  for (size_t i = 0; i < sizeof(bas_default_bindings) / sizeof(bas_default_bindings[0]); i++) {
    bas_restore_keyseq(&bas_default_bindings[i]);
  }
}

static int bas_interrupt(int count, int key) {
  (void)count;
  (void)key;

  bas_async_reset_state();
  bas_clear_suggestion();
  bas_drawn_rows = -1;
  bas_last_point = -1;
  bas_last_end = -1;
  bas_free(&bas_last_line);

  rl_replace_line("", 0);
  rl_point = 0;
  rl_done = 1;
  kill(getpid(), SIGINT);
  return 0;
}

static void bas_prep_terminal(int meta_flag) {
  if (bas_in_prep_terminal) {
    return;
  }

  bas_in_prep_terminal = 1;
  if (bas_original_prep_term_function &&
      bas_original_prep_term_function != bas_prep_terminal) {
    bas_original_prep_term_function(meta_flag);
  } else {
    rl_prep_terminal(meta_flag);
  }

  bas_set_input_isig(0, NULL);
  bas_in_prep_terminal = 0;
}

static int bas_install(void) {
  if (bas_installed) {
    return 1;
  }

  setlocale(LC_CTYPE, "");
  bas_set_default_async();
  bas_add_readline_functions();
  bas_bind_defaults();

  if (bas_original_keyboard_timeout < 0) {
    bas_original_keyboard_timeout = rl_set_keyboard_input_timeout(10000);
  } else {
    rl_set_keyboard_input_timeout(10000);
  }

  if (rl_event_hook != bas_event_hook) {
    bas_original_event_hook = rl_event_hook;
    rl_event_hook = bas_event_hook;
  }

  if (rl_prep_term_function != bas_prep_terminal) {
    bas_original_prep_term_function = rl_prep_term_function;
    rl_prep_term_function = bas_prep_terminal;
  }

  bas_installed = 1;
  bas_enabled = 1;
  return 1;
}

static void bas_uninstall(void) {
  if (!bas_installed) {
    return;
  }

  if (rl_event_hook == bas_event_hook) {
    rl_event_hook = bas_original_event_hook;
  }
  if (rl_prep_term_function == bas_prep_terminal) {
    rl_prep_term_function = bas_original_prep_term_function;
    bas_original_prep_term_function = NULL;
  }
  if (bas_original_keyboard_timeout >= 0) {
    rl_set_keyboard_input_timeout(bas_original_keyboard_timeout);
    bas_original_keyboard_timeout = -1;
  }
  bas_restore_defaults();
  bas_async_reset_state();
  bas_clear_suggestion();
  bas_free(&bas_last_line);
  bas_installed = 0;
}

static int bas_status(void) {
  printf("bash-autosuggestions: %s, %s\n", bas_installed ? "installed" : "not installed",
         bas_enabled ? "enabled" : "disabled");
  if (bas_suggestion) {
    printf("suggestion: %s\n", bas_suggestion);
  }
  return EXECUTION_SUCCESS;
}

static const char *bas_word(WORD_LIST *list) {
  if (!list || !list->word || !list->word->word) {
    return NULL;
  }
  return list->word->word;
}

static int bas_help_uses_color(void) {
  const char *term = getenv("TERM");
  return isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL &&
         (!term || strcmp(term, "dumb") != 0);
}

static int bas_print_help(void) {
  int color = bas_help_uses_color();
  const char *reset = color ? "\033[0m" : "";
  const char *title = color ? "\033[1;38;5;45m" : "";
  const char *heading = color ? "\033[1;38;5;39m" : "";
  const char *cmd = color ? "\033[1;38;5;220m" : "";
  const char *dim = color ? "\033[2m" : "";

  printf("\n%sbash_autosuggestions%s\n", title, reset);
  printf("  %sGhost-text command suggestions for interactive Bash.%s\n\n", dim, reset);
  printf("%sUsage%s\n", heading, reset);
  printf("  %sbash_autosuggestions%s [command]\n\n", cmd, reset);
  printf("%sCommands%s\n", heading, reset);
  printf("  %senable%s       enable autosuggestions in this shell\n", cmd, reset);
  printf("  %sdisable%s      disable autosuggestions in this shell\n", cmd, reset);
  printf("  %stoggle%s       toggle autosuggestions on or off\n", cmd, reset);
  printf("  %sclear%s        clear the visible suggestion\n", cmd, reset);
  printf("  %sfetch%s        refresh the suggestion for the current line\n", cmd, reset);
  printf("  %sbind%s         rebind configured Readline keys\n", cmd, reset);
  printf("  %sstatus%s       show whether the builtin is installed and enabled\n", cmd, reset);
  printf("  %suninstall%s    remove hooks and key bindings from this shell\n", cmd, reset);
  printf("  %sconfig%s       open the guided configuration UI when sourced through the loader\n",
         cmd, reset);
  printf("  %sconfigure%s    alias for config\n\n", cmd, reset);
  printf("%sOptions%s\n", heading, reset);
  printf("  %s-h, --help%s   show this help\n", cmd, reset);
  return EXECUTION_SUCCESS;
}

int bash_autosuggestions_builtin(WORD_LIST *list) {
  const char *cmd = bas_word(list);
  if (!cmd || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 ||
      strcmp(cmd, "help") == 0) {
    return bas_print_help();
  }

  if (strcmp(cmd, "enable") == 0) {
    bas_install();
    bas_enabled = 1;
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "disable") == 0) {
    bas_enabled = 0;
    bas_clear_suggestion();
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "toggle") == 0) {
    bas_enabled = !bas_enabled;
    if (!bas_enabled) {
      bas_clear_suggestion();
    }
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "clear") == 0) {
    bas_clear_suggestion();
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "fetch") == 0) {
    bas_fetch_and_set_suggestion();
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "bind") == 0 || strcmp(cmd, "rebind") == 0) {
    bas_bind_defaults();
    return EXECUTION_SUCCESS;
  }

  if (strcmp(cmd, "status") == 0) {
    return bas_status();
  }

  if (strcmp(cmd, "uninstall") == 0) {
    bas_uninstall();
    return EXECUTION_SUCCESS;
  }

  fprintf(stderr, "bash_autosuggestions: unknown command: %s\n", cmd);
  bas_print_help();
  return EX_USAGE;
}

struct builtin bash_autosuggestions_struct = {
    "bash_autosuggestions",
    bash_autosuggestions_builtin,
    BUILTIN_ENABLED,
    (char *const *)bas_long_doc,
    "bash_autosuggestions [command]",
    0,
};

int bash_autosuggestions_builtin_load(char *name) {
  (void)name;
  return bas_install();
}

void bash_autosuggestions_builtin_unload(char *name) {
  (void)name;
  bas_uninstall();
}
