#include <git2.h>
#include <unistd.h>
#include <string.h>
int main () {
	git_libgit2_init();
	sleep (1);
	git_merge_file_result out = { 0 }; // out.ptr will not receive a terminating null character
	git_merge_file_input libgit_base;
	git_merge_file_input libgit_our;
	git_merge_file_input libgit_their;
	git_merge_file_init_input(&libgit_base, GIT_MERGE_FILE_INPUT_VERSION);
	git_merge_file_init_input(&libgit_our, GIT_MERGE_FILE_INPUT_VERSION);
	git_merge_file_init_input(&libgit_their, GIT_MERGE_FILE_INPUT_VERSION);
	libgit_base.ptr = "A";
	libgit_base.size = strlen("A");
	libgit_our.ptr = "A";
	libgit_our.size = strlen("A");
	libgit_their.ptr = "A";
	libgit_their.size = strlen("A");
	git_merge_file (&out, &libgit_base, &libgit_our, &libgit_their, 0);
	git_merge_file_result_free (&out);
	git_libgit2_shutdown();
	sleep (1);
	return 0;
}
