/*
 * eCTF Collegiate 2020 miPod Example Code
 * Linux-side DRM driver
 */


#include "miPod.h"

#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <linux/gpio.h>
#include <string.h>
#include <signal.h>


volatile cmd_channel *c;

/*TODO MAJORS*/
/*
 * DONE - Change all strcpy to strncpy
 Partial (check last read) - change any gets/scanf/read/fgets to make sure that the sizes are correct
 DONE - change any printfs to __printf_chk (supposed to be secure and has checks/mitigations agains format strings exploits
 NOT DONE, THIS IS DRM/MB side - Remove some of the logging to screen as some have addresses and probably are not needed
 NOT DONE, I THINK THIS IS ALSO DRM/MB SIDE - Figure out why the program dumps data when closed and left in the background
 Partial, Handlers are there just need to finish shutdown; kill connections when program dies and close file_descriptors and tell program to stop playing if it is
 *
 */


//////////////////////// UTILITY FUNCTIONS ////////////////////////



// sends a command to the microblaze using the shared command channel and interrupt
void send_command(int cmd) {
	/*TODO check the devmem and try to use a different method.*/
		
    memcpy((void*)&c->cmd, &cmd, 1);

    //trigger gpio interrupt
    system("devmem 0x41200000 32 0");
    system("devmem 0x41200000 32 1");
}

void signal_handler(int sig){
	//logout if logged in
	if(c->drm_state!=STOPPED){
		send_command(STOP);
		while(c->drm_state!=STOPPED)continue;
	}
	if (c->login_status){
		
		send_command(LOGOUT);
		while(c->login_status)continue;
	}
    	munmap((void*)c, sizeof(cmd_channel));
	puts("Goodbye");
	exit(0);
	
}


// parses the input of a command with up to two arguments
// any arguments not present will be set to NULL
void parse_input(char *input, char **cmd, char **arg1, char **arg2) {
    *cmd = strtok(input, " \r\n");
    *arg1 = strtok(NULL, " \r\n");
    *arg2 = strtok(NULL, " \r\n");
}


// prints the help message while not in playback
void print_help() {
    mp_printf("miPod options:\r\n");
    mp_printf("  login <username> <pin>: log on to a miPod account (must be logged out)\r\n");
    mp_printf("  logout: log off of a miPod account (must be logged in)\r\n");
    mp_printf("  query <song.drm>: display information about the song\r\n");
    mp_printf("  share <song.drm> <username>: share the song with the specified user\r\n");
    mp_printf("  play <song.drm>: play the song\r\n");
    mp_printf("  digital_out <song.drm>: play the song to digital out\r\n");
    mp_printf("  exit: exit miPod\r\n");
    mp_printf("  help: display this message\r\n");
}


// prints the help message while in playback
void print_playback_help() {
    mp_printf("miPod playback options:\r\n");
    mp_printf("  stop: stop playing the song\r\n");
    mp_printf("  pause: pause the song\r\n");
    mp_printf("  resume: resume the paused song\r\n");
    mp_printf("  restart: restart the song\r\n");
    mp_printf("  ff: fast forwards 5 seconds(supported)\r\n");
    mp_printf("  rw: rewind 5 seconds (supported)\r\n");
    mp_printf("  help: display this message\r\n");
}


// loads a file into the song buffer with the associate
// returns the size of the file or 0 on error
size_t load_file(char *fname, char *song_buf) {
    int fd;
    struct stat sb;
	int length;
	if(fname == 0){
		print_help();
		return 0;
	}

	if(strlen(fname) > 64){
		puts("Song name is too long");
		return 0;
	}

    fd = open(fname, O_RDONLY);
    if (fd == -1){
        mp_printf("Failed to open file! Error = %d\r\n", errno);
        return 0;
    }

    if (fstat(fd, &sb) == -1){
        mp_printf("Failed to stat file! Error = %d\r\n", errno);
        return 0;
    }

	if(sb.st_size > MAX_SONG_SZ)
			length = MAX_SONG_SZ;
	else
			length = sb.st_size;
    read(fd, song_buf, length);
    close(fd);

    mp_printf("Loaded file into shared buffer (%dB)\r\n", length);
    return length;
}


//////////////////////// COMMAND FUNCTIONS ////////////////////////


// attempts to log in for a user
void login(char *username, char *pin) {
	
	/*checks to see if username or pin*/
    if (!username || !pin) {
        mp_printf("Invalid user name/PIN\r\n");
        print_help();
        return;
    }

    // drive DRM
    strncpy((void*)c->username, username, USERNAME_SZ);
    strncpy((void*)c->pin,pin,MAX_PIN_SZ);
    send_command(LOGIN);
}


// logs out for a user
void logout() {
    // drive DRM
    if(c->drm_state!=STOPPED){
	send_command(STOP);
	while(c->drm_state!=STOPPED)continue;
    }
    send_command(LOGOUT);
}


// queries the DRM about the player
// DRM will fill shared buffer with query content
void query_player() {
    // drive DRM
    send_command(QUERY_PLAYER);
    while (c->drm_state == STOPPED) continue; // wait for DRM to start working
    while (c->drm_state == WORKING) continue; // wait for DRM to dump file

    // print query results
    mp_printf("Regions: %s", q_region_lookup(c->query, 0));
    for (int i = 1; i < c->query.num_regions; i++) {
        __printf_chk(1,", %s", q_region_lookup(c->query, i));
    }
    puts("\r");

    mp_printf("Authorized users: ");
    if (c->query.num_users) {
        __printf_chk(1,"%s", q_user_lookup(c->query, 0));
        for (int i = 1; i < c->query.num_users; i++) {
            __printf_chk(1,", %s", q_user_lookup(c->query, i));
        }
    }
    puts("\r");
}


// queries the DRM about a song
// looks good
void query_song(char *song_name) {
    // load the song into the shared buffer
	
    if (!load_file(song_name, (void*)&c->song)) {
        mp_printf("Failed to load song!\r\n");
        return;
    }

    // drive DRM
    send_command(QUERY_SONG);
    while (c->drm_state == STOPPED) continue; // wait for DRM to start working
    while (c->drm_state == WORKING) continue; // wait for DRM to finish

    // print query results

    mp_printf("Regions: %s", q_region_lookup(c->query, 0));
    for (int i = 1; i < c->query.num_regions; i++) {
        __printf_chk(1,", %s", q_region_lookup(c->query, i));
    }
    puts("\r");

    mp_printf("Owner: %s", c->query.owner);
    puts("\r");

    mp_printf("Authorized users: ");
    if (c->query.num_users) {
        __printf_chk(1,"%s", q_user_lookup(c->query, 0));
        for (int i = 1; i < c->query.num_users; i++) {
            __printf_chk(1,", %s", q_user_lookup(c->query, i));
        }
    }
    puts("\r");
}


// attempts to share a song with a user
// TODO does this function need to open and write another file?
// TODO find where the wav_size is set
void share_song(char *song_name, char *username) {
    int fd;
    unsigned int length;
    ssize_t wrote, written = 0;

    if (!username || !song_name) {
        mp_printf("Need song name and username\r\n");
        print_help();
	return;
    }

    // load the song into the shared buffer
    if (!load_file(song_name, (void*)&c->song)) {
        mp_printf("Failed to load song!\r\n");
        return;
    }

    strncpy((char *)c->username, username,USERNAME_SZ);

    // drive DRM
    send_command(SHARE);
    while (c->drm_state == STOPPED) continue; // wait for DRM to start working
    while (c->drm_state == WORKING) continue; // wait for DRM to share song

    // request was rejected if WAV length is 0
    length = c->song.wav_size;
    if (length == 0) {
        mp_printf("Share rejected\r\n");
        return;
    }

    // open output file
    fd = open(song_name, O_WRONLY);
    if (fd == -1){
        mp_printf("Failed to open file! Error = %d\r\n", errno);
        return;
    }

    // write song dump to file
    mp_printf("Writing song to file '%s' (%dB)\r\n", song_name, length);
    while (written < length) {
        wrote = write(fd, (char *)&c->song + written, length - written);
        if (wrote == -1) {
            mp_printf("Error in writing file! Error = %d\r\n", errno);
            return;
        }
        written += wrote;
    }
    close(fd);
    mp_printf("Finished writing file\r\n");
}


// plays a song and enters the playback command loop
int play_song(char *song_name) {
    char usr_cmd[USR_CMD_SZ + 1], *cmd = NULL, *arg1 = NULL, *arg2 = NULL;

    // load song into shared buffer
    if (!load_file(song_name, (void*)&c->song)) {
        mp_printf("Failed to load song!\r\n");
        return 0;
    }

    // drive the DRM
    send_command(PLAY);
    while (c->drm_state == STOPPED) continue; // wait for DRM to start playing

    // play loop
    while(1) {
        // get a valid command
        do {
            print_prompt_msg(song_name);
            fgets(usr_cmd, USR_CMD_SZ, stdin);

            // exit playback loop if DRM has finished song
            if (c->drm_state == STOPPED) {
                mp_printf("Song finished\r\n");
                return 0;
            }
        } while (strlen(usr_cmd) < 2);

        // parse and handle command
        parse_input(usr_cmd, &cmd, &arg1, &arg2);
        if (!cmd) {
            continue;
        } else if (!strcmp(cmd, "help")) {
            print_playback_help();
        } else if (!strcmp(cmd, "resume")) {
            send_command(PLAY);
            usleep(200000); // wait for DRM to print
        } else if (!strcmp(cmd, "pause")) {
            send_command(PAUSE);
            usleep(200000); // wait for DRM to print
        } else if (!strcmp(cmd, "stop")) {
            send_command(STOP);
	    while(c->drm_state!=STOPPED)continue;
            usleep(200000); // wait for DRM to print
            break;
        } else if (!strcmp(cmd, "restart")) {
            send_command(RESTART);
        } else if (!strcmp(cmd, "exit")) {
            mp_printf("Exiting...\r\n");
            send_command(STOP);
	    while(c->drm_state!=STOPPED)continue;
            return -1;
        } else if (!strcmp(cmd, "rw")) {
	    mp_printf("Skipping Backward 5 Sec");
	    send_command(RW);
        } else if (!strcmp(cmd, "ff")) {
	    mp_printf("Skipping Forward 5 Sec");
	    send_command(FF);
        } else {
            mp_printf("Unrecognized command.\r\n\r\n");
            print_playback_help();
        }
    }

    return 0;
}


// turns DRM song into original WAV for digital output
// TODO Not finished needs to be a wav file outputed, and change name out out file
void digital_out(char *song_name) {
    char fname[128];

    // load file into shared buffer
    if (!load_file(song_name, (void*)&c->song)) {
        mp_printf("Failed to load song!\r\n");
        return;
    }

    // drive DRM
    send_command(DIGITAL_OUT);
    while (c->drm_state == STOPPED) continue; // wait for DRM to start working
    while (c->drm_state == WORKING) continue; // wait for DRM to dump file

    strncpy(fname,song_name, 64);
    strcat(fname, ".dout");

    // open digital output file
    int written = 0, wrote, length = c->song.file_size + 8;
    int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == -1){
        mp_printf("Failed to open file! Error = %d\r\n", errno);
        return;
    }
	if (length > MAX_SONG_SZ)
		length = MAX_SONG_SZ;

    // write song dump to file
    mp_printf("Writing song to file '%s' (%dB)\r\n", fname, length);
    while (written < length) {
        wrote = write(fd, (char *)&c->song + written, length - written);
        if (wrote == -1) {
            mp_printf("Error in writing file! Error = %d \r\n", errno);
            return;
        }
        written += wrote;
    }
    close(fd);
    mp_printf("Finished writing file\r\n");
}


//////////////////////// MAIN ////////////////////////


int main(int argc, char** argv)
{
	signal(SIGINT, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGABRT, signal_handler);
    int mem;
    char usr_cmd[USR_CMD_SZ + 1], *cmd = NULL, *arg1 = NULL, *arg2 = NULL;
    memset(usr_cmd, 0, USR_CMD_SZ + 1);

    // open command channel
    mem = open("/dev/uio0", O_RDWR);
	//TODO if possible remove mmap, it is not a good function to use sparily, we may need to keep it but with ASLR hopefully being turned on we can alter it
    c = mmap(NULL, sizeof(cmd_channel), PROT_READ | PROT_WRITE,
             MAP_SHARED, mem, 0);
    if (c == MAP_FAILED){
        mp_printf("MMAP Failed! Error = %d\r\n", errno);
        return -1;
    }
    mp_printf("Command channel open at %p (%dB)\r\n", c, sizeof(cmd_channel));

    // dump player information before command loop
    query_player();

    // go into command loop until exit is requested
    while (1) {
        // get command
        print_prompt();
		//This looks like the most secure part of the program
        fgets(usr_cmd, USR_CMD_SZ, stdin);

        // parse and handle command
        parse_input(usr_cmd, &cmd, &arg1, &arg2);
        if (!cmd) {
            continue;
        } else if (!strcmp(cmd, "help")) {
            print_help();
        } else if (!strcmp(cmd, "login")) {
            login(arg1, arg2);
        } else if (!strcmp(cmd, "logout")) {
            logout();
        } else if (!strcmp(cmd, "query")) {
            query_song(arg1);
        } else if (!strcmp(cmd, "play")) {
            // break if exit was commanded in play loop
            if (play_song(arg1) < 0) {
                break;
            }
        } else if (!strcmp(cmd, "digital_out")) {
            digital_out(arg1);
        } else if (!strcmp(cmd, "share")) {
            share_song(arg1, arg2);
        } else if (!strcmp(cmd, "exit")) {
            mp_printf("Exiting...\r\n");
            break;
        } else {
            mp_printf("Unrecognized command.\r\n\r\n");
            print_help();
        }
    }
    if (c->login_status)
	send_command(LOGOUT);
    // unmap the command channel
    munmap((void*)c, sizeof(cmd_channel));

    return 0;
}
