#include <fcntl.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

volatile sig_atomic_t keep_alive = 1;
typedef struct {
  unsigned int do_click : 1;
  unsigned int run : 1;
} left_click_loop_controller;

int setup_device() {
  printf("Setting up virtual device\n");
  struct uinput_setup usetup;
  int device = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

  // Enable left click, we don't need anything else for this macro
  ioctl(device, UI_SET_EVBIT, EV_KEY);
  ioctl(device, UI_SET_KEYBIT, BTN_LEFT);

  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_VIRTUAL;
  strcpy(usetup.name, "BTN_EXTRA as left click repeater");

  ioctl(device, UI_DEV_SETUP, &usetup);
  ioctl(device, UI_DEV_CREATE);

  // Allow userspace to detect the new device
  sleep(1);

  // At this point the device will be reported by `libinput list-devices`
  return device;
}

void drop_device(int device) {
  // Wait a bit to be sure the last event was executed ( hopefully a left click
  // release )
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
  // press and hold
  emit_input(fd, EV_KEY, BTN_LEFT, 1);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
  // release
  emit_input(fd, EV_KEY, BTN_LEFT, 0);
  emit_input(fd, EV_SYN, SYN_REPORT, 0);
}

void on_close_sig(int sig) {
  keep_alive = 0;
}

void *loop_left_click(void *arg) {
  left_click_loop_controller *ctrl = (left_click_loop_controller *)arg;
  int device = setup_device();

  printf("Macro is up\n");
  while (ctrl->run) {
    if (ctrl->do_click) {
      do_left_click(device);
    }
    usleep(50000);
  }

  drop_device(device);
  return NULL;
}

int main(int arg_amnt, char *argv[]) {

  if (arg_amnt != 2) {
    printf("  Usage: %s <path_to_device>\n  You can find the device path with "
           "`sudo libinput list-devices`\n",
           argv[0]);
    return 1;
  }

  struct input_event ev;
  left_click_loop_controller ctrl;
  pthread_t virtual_input_thread;

  int mouse = open(argv[1], O_RDONLY);

  if (mouse < 0) {
    perror("Failed to open input device");
    return 1;
  }

  signal(SIGINT, on_close_sig);
  signal(SIGTERM, on_close_sig);

  ctrl.run = 1;
  pthread_create(&virtual_input_thread, NULL, loop_left_click, &ctrl);

  while (keep_alive) {
    if (read(mouse, &ev, sizeof(ev)) == sizeof(ev)) {
      if (ev.code == BTN_EXTRA && ev.type == EV_KEY) {
        ctrl.do_click = ev.value == 1;
      }
    }
  }

  ctrl.run = 0;
  pthread_join(virtual_input_thread, NULL);
  close(mouse);
  return 0;
}
