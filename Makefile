PREFIX ?= /usr/local
INSTALL_USER_HOME ?= $(shell if [ -n "$$SUDO_USER" ] && [ "$$SUDO_USER" != root ]; then home=$$(getent passwd "$$SUDO_USER" 2>/dev/null | awk -F: '{print $$6}'); if [ -z "$$home" ] && [ -d "/home/$$SUDO_USER" ]; then home="/home/$$SUDO_USER"; fi; printf '%s' "$${home:-$$HOME}"; else printf '%s' "$$HOME"; fi)
BASHRC ?= $(INSTALL_USER_HOME)/.bashrc
BASHRC_UPDATE ?= ask
CC ?= cc
PKG_CONFIG ?= pkg-config

BASH_INCLUDE ?= $(shell $(PKG_CONFIG) --variable=includedir bash 2>/dev/null || printf '%s/include' "$${PREFIX:-/usr/local}")
READLINE_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags readline 2>/dev/null)
READLINE_LIBS ?= $(shell $(PKG_CONFIG) --libs readline 2>/dev/null || printf '%s' '-lreadline')

CFLAGS ?= -O2 -g
CPPFLAGS += -I$(BASH_INCLUDE) -I$(BASH_INCLUDE)/bash/include -I$(BASH_INCLUDE)/bash/builtins $(READLINE_CFLAGS)
LDFLAGS += -shared
LDLIBS += $(READLINE_LIBS)

TARGET = bash-autosuggestions.so
SRC = src/bash_autosuggestions.c
INSTALL_DIR = $(DESTDIR)$(PREFIX)/lib/bash-autosuggestions
BASHRC_SOURCE = $(PREFIX)/lib/bash-autosuggestions/bash-autosuggestions.bash

.DEFAULT_GOAL := help

COLOR ?= auto
COLOR_ENABLED :=
ifeq ($(COLOR),always)
COLOR_ENABLED := 1
else ifeq ($(COLOR),never)
COLOR_ENABLED :=
else ifneq ($(NO_COLOR),)
COLOR_ENABLED :=
else ifneq ($(MAKE_TERMOUT),)
COLOR_ENABLED := 1
endif

ifeq ($(COLOR_ENABLED),1)
COLOR_STEP := \033[1;36m
COLOR_OK := \033[1;32m
COLOR_TITLE := \033[1;37m
COLOR_TARGET := \033[36m
COLOR_DIM := \033[2m
COLOR_RESET := \033[0m
endif

PRINT_STEP = @printf '$(COLOR_STEP)==>$(COLOR_RESET) %s\n' '$(1)'
PRINT_OK = @printf '$(COLOR_OK)OK:$(COLOR_RESET) %s\n' '$(1)'

.PHONY: all clean test install bashrc-snippet help

all: $(TARGET) ## Build the Bash loadable builtin

$(TARGET): $(SRC)
	$(call PRINT_STEP,Building $(TARGET))
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC $(LDFLAGS) -o $@ $< $(LDLIBS)
	$(call PRINT_OK,Built $(TARGET))

test: $(TARGET) ## Run the smoke test suite
	$(call PRINT_STEP,Running smoke tests)
	tests/smoke.sh
	$(call PRINT_OK,Tests passed)

install: $(TARGET) ## Install files and offer to update ~/.bashrc
	$(call PRINT_STEP,Installing to $(INSTALL_DIR))
	install -d "$(INSTALL_DIR)"
	install -m 755 $(TARGET) "$(INSTALL_DIR)/$(TARGET)"
	install -m 644 bash-autosuggestions.bash "$(INSTALL_DIR)/bash-autosuggestions.bash"
	$(call PRINT_OK,Installed files)
	@printf '\nAdd this to %s to load bash-autosuggestions:\n\n' "$(BASHRC)"
	@$(MAKE) --no-print-directory bashrc-snippet
	@printf '\n'
	@set -eu; \
	update="$(BASHRC_UPDATE)"; \
	case "$$update" in \
	  yes|y|1|true|TRUE) answer=y ;; \
	  no|n|0|false|FALSE) answer=n ;; \
	  ask) \
	    if [ -t 0 ]; then \
	      printf 'Write/update this managed block in %s? [Y/n]: ' "$(BASHRC)"; \
	      read answer; \
	      answer=$${answer:-y}; \
	    else \
	      answer=n; \
	      printf 'Not updating %s because stdin is not interactive. Run make install BASHRC_UPDATE=yes to update it.\n' "$(BASHRC)"; \
	    fi ;; \
	  *) printf 'Invalid BASHRC_UPDATE=%s (use ask, yes, or no)\n' "$$update" >&2; exit 2 ;; \
	esac; \
	case "$$answer" in \
	  y|Y|yes|YES) \
	    tmp="$$(mktemp "$${TMPDIR:-/tmp}/bash-autosuggestions.XXXXXX")"; \
	    if [ -f "$(BASHRC)" ]; then \
	      awk -v source_line="source $(BASHRC_SOURCE)" '\
	        BEGIN { replaced = 0 } \
	        $$0 == "# >>> bash-autosuggestions >>>" { in_block = 1; next } \
	        $$0 == "# <<< bash-autosuggestions <<<" { in_block = 0; next } \
	        in_block { next } \
	        $$0 ~ /^[[:space:]]*(source|\.)[[:space:]].*bash-autosuggestions[.]bash([[:space:]]|$$)/ { \
	          if (!replaced) { print source_line; replaced = 1 } \
	          next \
	        } \
	        { print } \
	        END { if (!replaced) { if (NR > 0) print ""; print source_line } } \
	      ' "$(BASHRC)" >"$$tmp"; \
	    else \
	      printf '%s\n' "source $(BASHRC_SOURCE)" >"$$tmp"; \
	    fi; \
	    mkdir -p "$$(dirname -- "$(BASHRC)")"; \
	    cat "$$tmp" >"$(BASHRC)"; \
	    if [ -n "$${SUDO_USER:-}" ] && [ "$$SUDO_USER" != root ]; then \
	      case "$(BASHRC)" in \
	        "$(INSTALL_USER_HOME)"/*) chown "$$SUDO_USER:" "$(BASHRC)" 2>/dev/null || chown "$$SUDO_USER" "$(BASHRC)" 2>/dev/null || true ;; \
	      esac; \
	    fi; \
	    rm -f "$$tmp"; \
	    printf 'Updated %s.\n' "$(BASHRC)"; \
	    printf '\nNext steps:\n'; \
	    printf '  source %s\n' "$(BASHRC)"; \
	    printf '  # or open a new terminal\n'; \
	    printf '\nAfter that, the `bash_autosuggestions` command is available.\n' ;; \
	  *) \
	    printf 'Skipped updating %s.\n' "$(BASHRC)"; \
	    printf '\nNext steps:\n'; \
	    printf '  Add the block shown above to %s, then run:\n' "$(BASHRC)"; \
	    printf '  source %s\n' "$(BASHRC)"; \
	    printf '  # or open a new terminal after editing it\n'; \
	    printf '\nAfter that, the `bash_autosuggestions` command is available.\n' ;; \
	esac

bashrc-snippet: ## Print the ~/.bashrc source line
	@printf 'source %s\n' "$(BASHRC_SOURCE)"

clean: ## Remove local build artifacts
	$(call PRINT_STEP,Cleaning)
	rm -f $(TARGET)
	$(call PRINT_OK,Clean complete)

help: ## Show available Make targets
	@printf '$(COLOR_TITLE)%s$(COLOR_RESET)\n' 'bash-autosuggestions'
	@printf '$(COLOR_DIM)%s$(COLOR_RESET)\n' '===================='
	@awk -v c='$(COLOR_TARGET)' -v r='$(COLOR_RESET)' 'BEGIN {FS = ":.*## "}; /^[A-Za-z0-9_.-]+:.*## / {printf "%s%-16s%s %s\n", c, $$1, r, $$2}' $(MAKEFILE_LIST) | sort
	@printf '\n$(COLOR_DIM)%s$(COLOR_RESET)\n' 'Common variables:'
	@printf '  %-16s %s\n' 'PREFIX' 'install prefix (default: /usr/local)'
	@printf '  %-16s %s\n' 'BASHRC' 'bash startup file to update (default: invoking user ~/.bashrc under sudo)'
	@printf '  %-16s %s\n' 'BASHRC_UPDATE' 'ask, yes, or no (default: ask)'
