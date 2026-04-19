#include <fcntl.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CLIPBOARD_FILE "/tmp/regex_click_clipboard_watch"

volatile sig_atomic_t keep_alive = 1;

typedef struct {
  // not bothering with mutexes for this since single bit are atomic enough for my usecase
  unsigned int do_click : 1; // set to 1 when the user is holding the extra btn
  unsigned int run : 1;
  unsigned int can_click: 1; // set to 1 when the clipboard content was checked and the left click can be performed
  regex_t regex;
} thread_controls;

// --- Clipboard watcher thread ---

void *watch_clipboard(void *arg) {
  thread_controls *ctrl = (thread_controls *)arg;

  int inotify_fd = inotify_init();
  if (inotify_fd < 0) {
    perror("inotify_init failed");
    return NULL;
  }

  // Ensure the file exists before watching
  FILE *f = fopen(CLIPBOARD_FILE, "a");
  if (f)
    fclose(f);

  int wd = inotify_add_watch(inotify_fd, CLIPBOARD_FILE, IN_CLOSE_WRITE | IN_MODIFY);
  if (wd < 0) {
    perror("inotify_add_watch failed");
    close(inotify_fd);
    return NULL;
  }

  char event_buf[sizeof(struct inotify_event) + 256];

  while (keep_alive) {
    // Use select() with a timeout so we can check keep_alive periodically
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(inotify_fd, &fds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};

    if (select(inotify_fd + 1, &fds, NULL, NULL, &tv) <= 0)
      continue;

    read(inotify_fd, event_buf, sizeof(event_buf));

    // Read clipboard file content
    FILE *fp = fopen(CLIPBOARD_FILE, "r");
    if (!fp)
      continue;

    char content[4096] = {0};
    fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);

    // Strip trailing newline for cleaner matching
    size_t len = strlen(content);
    if (len > 0 && content[len - 1] == '\n')
      content[len - 1] = '\0';

    if (regexec(&ctrl->regex, content, 0, NULL, 0) == 0) {
      printf("Clipboard matched regex, stopping clicks\n");
      keep_alive = 0;
    } else {
        printf("Regex doesn't match item, can click\n");
        ctrl->can_click = 1;
    }
  }

  inotify_rm_watch(inotify_fd, wd);
  close(inotify_fd);
  return NULL;
}

// --- uinput device setup ---

int setup_device() {
  printf("Setting up virtual device\n");
  struct uinput_setup usetup;
  int device = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  ioctl(device, UI_SET_EVBIT, EV_KEY);
  ioctl(device, UI_SET_KEYBIT, BTN_LEFT);
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_VIRTUAL;
  strcpy(usetup.name, "BTN_EXTRA as left click repeater");
  ioctl(device, UI_DEV_SETUP, &usetup);
  ioctl(device, UI_DEV_CREATE);
  sleep(1);
  return device;
}

int setup_keyboard_device() {
  printf("Setting up virtual keyboard device\n");
  struct uinput_setup usetup;
  int device = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

  ioctl(device, UI_SET_EVBIT, EV_KEY);
  ioctl(device, UI_SET_KEYBIT, KEY_LEFTCTRL);
  ioctl(device, UI_SET_KEYBIT, KEY_C);

  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_VIRTUAL;
  strcpy(usetup.name, "Virtual Keyboard for Ctrl+C");

  ioctl(device, UI_DEV_SETUP, &usetup);
  ioctl(device, UI_DEV_CREATE);

  sleep(1);
  return device;
}

void drop_device(int device) {
  printf("Destroying virtual device\n");
  sleep(1);
  ioctl(device, UI_DEV_DESTROY);
  close(device);
}

void emit_input(int fd, int type, int code, int val) {
  struct input_event ie;
  ie.type = type;
  ie.code = code;
  ie.value = val;
  write(fd, &ie, sizeof(ie));
}

void do_left_click(int fd) {
  emit_input(fd, EV_KEY, BTN_LEFT, 1);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
  emit_input(fd, EV_KEY, BTN_LEFT, 0);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
}

void do_ctrl_c(int fd) {
  emit_input(fd, EV_KEY, KEY_LEFTCTRL, 1);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
  usleep(5000); // Small delay for modifier
  emit_input(fd, EV_KEY, KEY_C, 1);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
  usleep(5000);
  emit_input(fd, EV_KEY, KEY_C, 0);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
  usleep(5000);
  emit_input(fd, EV_KEY, KEY_LEFTCTRL, 0);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
}

void on_close_sig(int sig) { keep_alive = 0; }

void *loop_left_click(void *arg) {
  thread_controls *ctrl = (thread_controls *)arg;
  int mouse_device = setup_device();
  int keyboard_device = setup_keyboard_device();

  printf("Macro is up! Don't focus the terminal that is running the script otherwise the Ctrl+C will kill this process\n");
  sleep(2); // small sleep just to leave the terminal before the script stops itself
  while (ctrl->run) {
    if (ctrl->do_click && ctrl->can_click) {
      if(keep_alive) {
          printf("Do left click\n");
          do_left_click(mouse_device);
          ctrl->can_click = 0;
      }
    } else if(ctrl->can_click == 0) {
        printf("Do ctrl c\n");
        // clipboard thread will se can_click to 1
        do_ctrl_c(keyboard_device);
    }

    usleep(50000);
  }

  drop_device(mouse_device);
  drop_device(keyboard_device);
  return NULL;
}

// --- Main ---

int main(int arg_amnt, char *argv[]) {
  setvbuf(stdout, NULL, _IONBF, 0);

  if (arg_amnt != 3) {
      printf("  Usage: %s <path_to_device> <regex>\n"
             "  You can find the device path with `sudo libinput list-devices`\n"
             "  Example: %s /dev/input/event6 'd i'\n",
             argv[0], argv[0]);
    return 1;
  }

  struct input_event ev;
  thread_controls ctrl;
  pthread_t virtual_input_thread;
  pthread_t clipboard_thread;

  // Compile regex
  if (regcomp(&ctrl.regex, argv[2], REG_EXTENDED | REG_ICASE | REG_NEWLINE | REG_NOSUB) !=
      0) {
    perror("Failed to compile regex");
    return 1;
  }

  int mouse = open(argv[1], O_RDONLY);
  if (mouse < 0) {
    perror("Failed to open input device");
    regfree(&ctrl.regex);
    return 2;
  }

  signal(SIGINT, on_close_sig);
  signal(SIGTERM, on_close_sig);

  // Launch wl-paste --watch to write clipboard changes to the file
  // Done _before_ threads are up so inotify is not watching and the current clipboard
  // doesn't interact with the script
  pid_t wl_paste_pid = fork();
  if (wl_paste_pid == 0) {
    execlp("wl-paste", "wl-paste", "--watch", "tee", CLIPBOARD_FILE, NULL);
    perror("execlp wl-paste failed");
    exit(1);
  }

  ctrl.run = 1;
  pthread_create(&virtual_input_thread, NULL, loop_left_click, &ctrl);
  pthread_create(&clipboard_thread, NULL, watch_clipboard, &ctrl);

  while (keep_alive) {
    if (read(mouse, &ev, sizeof(ev)) == sizeof(ev)) {
      if (ev.code == BTN_EXTRA && ev.type == EV_KEY) {
        ctrl.do_click = ev.value == 1 || ev.value == 2;
      }
    }
  }

  ctrl.run = 0;
  kill(wl_paste_pid, SIGTERM);
  pthread_join(virtual_input_thread, NULL);
  pthread_join(clipboard_thread, NULL);
  close(mouse);
  regfree(&ctrl.regex);
  return 0;
}
