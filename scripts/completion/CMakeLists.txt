file (READ "kdb_zsh_completion" CONTENT_OF_ZSH_COMP)
file (READ "kdb-bash-completion" CONTENT_OF_BASH_COMP)
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/install-sh-completion.in" "${CMAKE_CURRENT_BINARY_DIR}/install-sh-completion" @ONLY)
install (
	PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/install-sh-completion"
	DESTINATION ${TARGET_TOOL_EXEC_FOLDER}
	COMPONENT elektra-bin)

if (INSTALL_SYSTEM_FILES)
	if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		set (
			BASH_COMPLETION_COMPLETIONSDIR
			"/usr/local/share/bash-completion/completions"
			CACHE INTERNAL "bash completions dir")
	else ()
		set (
			BASH_COMPLETION_COMPLETIONSDIR
			"/usr/share/bash-completion/completions"
			CACHE INTERNAL "bash completions dir")
	endif ()
	find_package (bash-completion QUIET)
	unset (bash-completion_DIR CACHE)
	if (NOT BASH_COMPLETION_FOUND)
		find_package (PkgConfig QUIET)
		if (PKG_CONFIG_FOUND)
			pkg_check_modules (BASH_COMPLETION bash-completion QUIET)
			if (BASH_COMPLETION_FOUND)
				pkg_get_variable (BASH_COMPLETION_COMPLETIONSDIR bash-completion completionsdir)
			endif ()
		endif ()
	endif ()
	install (
		FILES kdb-bash-completion
		DESTINATION ${BASH_COMPLETION_COMPLETIONSDIR}
		RENAME kdb
		COMPONENT elektra-bin)
	if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		set (FISH_COMPLETION_COMPLETIONSDIR "/usr/local/share/fish/completions")
	else ()
		set (FISH_COMPLETION_COMPLETIONSDIR "/usr/share/fish/vendor_completions.d")
	endif ()
	find_package (PkgConfig QUIET)
	if (PKG_CONFIG_FOUND)
		pkg_check_modules (FISH_FOUND fish QUIET)
		if (FISH_FOUND)
			pkg_get_variable (FISH_COMPLETION_COMPLETIONSDIR fish completionsdir)
		endif ()
	endif ()
	install (
		FILES kdb.fish
		DESTINATION ${FISH_COMPLETION_COMPLETIONSDIR}
		COMPONENT elektra-bin)

	if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

		# On macOS we need to copy this into another folder. Because of "System Integrity Protection", we need to use a /usr/local
		# path
		install (
			FILES kdb_zsh_completion
			DESTINATION /usr/local/share/zsh/site-functions
			RENAME _kdb
			COMPONENT elektra-bin)
	else ()

		# This Folder is at least the valid folder for Debian
		install (
			FILES kdb_zsh_completion
			DESTINATION /usr/share/zsh/vendor-completions
			RENAME _kdb
			COMPONENT elektra-bin)
	endif ()
endif ()
