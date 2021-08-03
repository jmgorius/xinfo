/**
 * xinfo - Dump information about the currently running X server instance.
 *
 * This is free and unencumbered software released into the public domain.
 *
 */

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void die(const char *msg) {
  fprintf(stderr, "FATAL ERROR: %s\n", msg);
  exit(1);
}

static ssize_t read_n(int fd, void *buffer, size_t n) {
  char *buf = buffer;
  size_t total_read;
  for (total_read = 0; total_read < n;) {
    ssize_t num_read = read(fd, buf, n - total_read);
    if (num_read == 0) /* EOF */
      return total_read;
    if (num_read == -1) {
      if (errno == EINTR)
        continue; /* We got interrupted, try again */
      return -1;
    }
    total_read += num_read;
    buf += num_read;
  }
  return total_read;
}

static ssize_t write_n(int fd, const void *buffer, size_t n) {
  const char *buf = buffer;
  size_t total_written;
  for (total_written = 0; total_written < n;) {
    ssize_t num_written = write(fd, buf, n - total_written);
    if (num_written <= 0) {
      if (num_written == -1 && errno == EINTR)
        continue; /* We got interrupted, try again */
      return -1;
    }
    total_written += num_written;
    buf += num_written;
  }
  return total_written;
}

#define MAX_XAUTHORITY_ENTRIES 32

#define X_VERSION_MAJOR 11
#define X_VERSION_MINOR 0
#define X_BASE_TCP_PORT 6000

#define X_PADDING(x) ((4 - ((x) % 4)) % 4)
#define X_PAD(x) ((x) + X_PADDING((x)))

#define X_REPLY 1
#define X_CONNECTION_STATUS_SUCCESS 1
#define X_BYTE_ORDER_LITTLE_ENDIAN 0
#define X_BITMAP_FORMAT_BIT_ORDER_LEAST_SIGNIFICANT 0
#define X_BACKING_STORES_NEVER 0
#define X_BACKING_STORES_WHEN_MAPPED 1

#define X_EVENT_MASK_KEY_PRESS 0x00000001u
#define X_EVENT_MASK_KEY_RELEASE 0x00000002u
#define X_EVENT_MASK_BUTTON_PRESS 0x00000004u
#define X_EVENT_MASK_BUTTON_RELEASE 0x00000008u
#define X_EVENT_MASK_ENTER_WINDOW 0x00000010u
#define X_EVENT_MASK_LEAVE_WINDOW 0x00000020u
#define X_EVENT_MASK_POINTER_MOTION 0x00000040u
#define X_EVENT_MASK_POINTER_MOTION_HINT 0x00000080u
#define X_EVENT_MASK_BUTTON1_MOTION 0x00000100u
#define X_EVENT_MASK_BUTTON2_MOTION 0x00000200u
#define X_EVENT_MASK_BUTTON3_MOTION 0x00000400u
#define X_EVENT_MASK_BUTTON4_MOTION 0x00000800u
#define X_EVENT_MASK_BUTTON5_MOTION 0x00001000u
#define X_EVENT_MASK_BUTTON_MOTION 0x00002000u
#define X_EVENT_MASK_KEYMAP_STATE 0x00004000u
#define X_EVENT_MASK_EXPOSURE 0x00008000u
#define X_EVENT_MASK_VISIBILITY_CHANGE 0x00010000u
#define X_EVENT_MASK_STRUCTURE_NOTIFY 0x00020000u
#define X_EVENT_MASK_RESIZE_REDIRECT 0x00040000u
#define X_EVENT_MASK_SUBSTRUCTURE_NOTIFY 0x00080000u
#define X_EVENT_MASK_SUBSTRUCTURE_REDIRECT 0x00100000u
#define X_EVENT_MASK_FOCUS_CHANGE 0x00200000u
#define X_EVENT_MASK_PROPERTY_CHANGE 0x00400000u
#define X_EVENT_MASK_COLORMAP_CHANGE 0x00800000u
#define X_EVENT_MASK_OWNER_GRAB_BUTTON 0x01000000u

#define X_OPCODE_GET_FONT_PATH 52
#define X_OPCODE_QUERY_EXTENSION 98
#define X_OPCODE_LIST_EXTENSIONS 99

#define X_EXTENSION_NAME_BIG_REQUESTS "BIG-REQUESTS"
#define X_EXTENSION_NAME_COMPOSITE "Composite"
#define X_EXTENSION_NAME_DAMAGE "DAMAGE"
#define X_EXTENSION_NAME_DOUBLE_BUFFER "DOUBLE-BUFFER"
#define X_EXTENSION_NAME_DPMS "DPMS"
#define X_EXTENSION_NAME_DRI2 "DRI2"
#define X_EXTENSION_NAME_DRI3 "DRI3"
#define X_EXTENSION_NAME_GLX "GLX"

#define X_OPCODE_BIG_REQUESTS_ENABLE 0
#define X_OPCODE_COMPOSITE_QUERY_VERSION 0
#define X_OPCODE_DAMAGE_QUERY_VERSION 0
#define X_OPCODE_DOUBLE_BUFFER_GET_VERSION 0
#define X_OPCODE_DPMS_GET_VERSION 0
#define X_OPCODE_DPMS_CAPABLE 1
#define X_OPCODE_DPMS_GET_TIMEOUTS 2
#define X_OPCODE_DRI2_QUERY_VERSION 0
#define X_OPCODE_DRI3_QUERY_VERSION 0
#define X_OPCODE_GLX_QUERY_VERSION 7

struct x_setup_request {
  uint8_t byte_order; /* Either 'B' for big endian, or 'l' for little endian */
  uint8_t pad1;
  uint16_t protocol_version_major;
  uint16_t protocol_version_minor;
  uint16_t auth_protocol_name_len;
  uint16_t auth_data_len;
  uint16_t pad2;
};

struct x_setup_response {
  uint8_t status;
  uint8_t failure_reason_length;
  uint16_t protocol_version_major;
  uint16_t protocol_version_minor;
  uint16_t additional_data_len;
};

struct x_setup_data_impl {
  uint32_t release_number;
  uint32_t resource_id_base;
  uint32_t resource_id_mask;
  uint32_t motion_buffer_size;
  uint16_t vendor_length;
  uint16_t maximum_request_len;
  uint8_t num_roots;
  uint8_t num_pixmap_formats;
  uint8_t image_byte_order;
  uint8_t bitmap_format_bit_order;
  uint8_t bitmap_format_scanline_unit;
  uint8_t bitmap_format_scanline_pad;
  uint8_t min_keycode;
  uint8_t max_keycode;
  uint32_t pad;
};

struct x_format {
  uint8_t depth;
  uint8_t bits_per_pixel;
  uint8_t scanline_pad;
  uint8_t pad[5];
};

struct x_screen_data {
  uint32_t root;
  uint32_t default_colormap;
  uint32_t white_pixel;
  uint32_t black_pixel;
  uint32_t current_input_mask;
  uint16_t width_in_pixels;
  uint16_t height_in_pixels;
  uint16_t width_in_millimeters;
  uint16_t height_in_millimeters;
  uint16_t min_installed_maps;
  uint16_t max_installed_maps;
  uint32_t root_visual_id;
  uint8_t backing_stores;
  uint8_t save_unders;
  uint8_t root_depth;
  uint8_t num_allowed_depths;
};

struct x_depth_data {
  uint8_t depth;
  uint8_t pad1;
  uint16_t num_visuals;
  uint32_t pad2;
};

struct x_visual_type {
  uint32_t visual_id;
  uint8_t visual_class;
  uint8_t bits_per_rgb_value;
  uint16_t colormap_entries;
  uint32_t red_mask;
  uint32_t green_mask;
  uint32_t blue_mask;
  uint32_t pad;
};

struct x_depth {
  struct x_depth_data data;
  struct x_visual_type *visuals;
};

struct x_screen {
  struct x_screen_data data;
  struct x_depth *allowed_depths;
};

struct x_setup_data {
  struct x_setup_data_impl data;
  char *vendor_name;
  struct x_format *pixmap_formats;
  struct x_screen *roots;
  uint16_t version_major;
  uint16_t version_minor;
};

static struct {
  int fd;
  struct x_setup_data setup_data;
  unsigned int big_requests_opcode;
  unsigned int composite_opcode;
  unsigned int damage_opcode;
  unsigned int double_buffer_opcode;
  unsigned int dpms_opcode;
  unsigned int dri2_opcode;
  unsigned int dri3_opcode;
  unsigned int glx_opcode;
} x_connection = {
    .fd = -1,
};

struct x_get_font_path_request {
  uint8_t opcode;
  uint8_t pad;
  uint16_t request_len;
};

struct x_get_font_path_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t data_len;
  uint16_t num_strings;
  uint8_t pad[22];
};

struct x_list_extensions_request {
  uint8_t opcode;
  uint8_t pad;
  uint16_t request_len;
};

struct x_list_extensions_reply {
  uint8_t status;
  uint8_t num_names;
  uint16_t sequence_number;
  uint32_t data_len;
  uint8_t pad[24];
};

struct x_query_extension_request {
  uint8_t opcode;
  uint8_t pad1;
  uint16_t request_len;
  uint16_t name_len;
  uint16_t pad2;
};

struct x_query_extension_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t data_len;
  uint8_t present;
  uint8_t major_opcode;
  uint8_t first_event;
  uint8_t first_error;
  uint8_t pad[20];
};

struct x_big_requests_enable_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
};

struct x_big_requests_enable_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t max_request_len;
  uint8_t pad[18];
};

struct x_composite_query_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint32_t version_major;
  uint32_t version_minor;
};

struct x_composite_query_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t version_major;
  uint32_t version_minor;
  uint8_t pad[16];
};

struct x_damage_query_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint32_t version_major;
  uint32_t version_minor;
};

struct x_damage_query_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t version_major;
  uint32_t version_minor;
  uint8_t pad[16];
};

struct x_double_buffer_get_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint8_t version_major;
  uint8_t version_minor;
  uint16_t pad;
};

struct x_double_buffer_get_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint8_t version_major;
  uint8_t version_minor;
  uint8_t pad[22];
};

struct x_dpms_get_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint16_t version_major;
  uint16_t version_minor;
};

struct x_dpms_get_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint16_t version_major;
  uint16_t version_minor;
  uint8_t pad[20];
};

struct x_dpms_capable_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
};

struct x_dpms_capable_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint8_t capable;
  uint8_t pad[23];
};

struct x_dpms_get_timeouts_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
};

struct x_dpms_get_timeouts_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint16_t standby_timeout;
  uint16_t suspend_timeout;
  uint16_t off_timeout;
  uint8_t pad[18];
};

struct x_dri2_query_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint32_t version_major;
  uint32_t version_minor;
};

struct x_dri2_query_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t version_major;
  uint32_t version_minor;
  uint8_t pad[16];
};

struct x_dri3_query_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint32_t version_major;
  uint32_t version_minor;
};

struct x_dri3_query_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t version_major;
  uint32_t version_minor;
  uint8_t pad[16];
};

struct x_glx_query_version_request {
  uint8_t opcode;
  uint8_t extension_opcode;
  uint16_t request_len;
  uint32_t version_major;
  uint32_t version_minor;
};

struct x_glx_query_version_reply {
  uint8_t status;
  uint8_t pad1;
  uint16_t sequence_number;
  uint32_t additional_data_len;
  uint32_t version_major;
  uint32_t version_minor;
  uint8_t pad[16];
};

static void x_disconnect(void) {
  if (x_connection.fd != -1) {
    close(x_connection.fd);
    x_connection.fd = -1;
  }

  if (x_connection.setup_data.roots) {
    for (size_t i = 0; i < x_connection.setup_data.data.num_roots; ++i) {
      if (x_connection.setup_data.roots[i].allowed_depths) {
        for (size_t j = 0;
             j < x_connection.setup_data.roots[i].data.num_allowed_depths; ++j)
          free(x_connection.setup_data.roots[i].allowed_depths[j].visuals);
        free(x_connection.setup_data.roots[i].allowed_depths);
      }
    }
    free(x_connection.setup_data.roots);
  }
  free(x_connection.setup_data.pixmap_formats);
  free(x_connection.setup_data.vendor_name);
}

static void x_connect_to_socket(int fd, size_t protocol_name_len,
                                const char *protocol_name, size_t auth_data_len,
                                const char *auth_data) {
  x_connection.fd = fd;

  /* Build the connection request to send to the X server */
  struct x_setup_request setup_request = {
      .byte_order = 'l', /* little endian */
      .protocol_version_major = X_VERSION_MAJOR,
      .protocol_version_minor = X_VERSION_MINOR,
      .auth_protocol_name_len = protocol_name_len,
      .auth_data_len = auth_data_len,
  };
  size_t protocol_len = X_PAD(protocol_name_len);
  size_t data_len = X_PAD(auth_data_len);
  size_t total_request_size = sizeof(setup_request) + protocol_len + data_len;

  /* Put all the required data in a single buffer instead of needlessly calling
   * write multiple times for the initial data and then for the authentication
   * info */
  char *request_buffer = calloc(total_request_size, 1);
  if (!request_buffer) {
    x_disconnect();
    die("Memory allocation failed");
  }
  memcpy(request_buffer, &setup_request, sizeof(setup_request));
  memcpy(request_buffer + sizeof(setup_request), protocol_name,
         protocol_name_len);
  memcpy(request_buffer + sizeof(setup_request) + protocol_len, auth_data,
         auth_data_len);

  /* Send the connection request to the server */
  ssize_t num_written =
      write_n(x_connection.fd, request_buffer, total_request_size);
  free(request_buffer);
  if (num_written != (ssize_t)total_request_size)
    die("Failed to send connection request to X server");

  /* Read the connection setup response */
  struct x_setup_response response = {};
  ssize_t num_read = read_n(x_connection.fd, &response, sizeof(response));
  if (num_read != sizeof(response)) {
    x_disconnect();
    die("Failed to read connection setup response from X server");
  }

  /* Read additional data */
  size_t additional_data_len = 4 * response.additional_data_len;
  char *additional_data = calloc(additional_data_len, 1);
  if (!additional_data)
    die("Memory allocation failed");
  num_read = read_n(x_connection.fd, additional_data, additional_data_len);
  if (num_read != (ssize_t)additional_data_len) {
    free(additional_data);
    x_disconnect();
    die("Failed to read additional connection information from X server");
  }

  if (response.status != X_CONNECTION_STATUS_SUCCESS) {
    fprintf(stderr, "ERROR: %s\n", additional_data);
    free(additional_data);
    x_disconnect();
    die("Connection to X server failed");
  }

  x_connection.setup_data.version_major = response.protocol_version_major;
  x_connection.setup_data.version_minor = response.protocol_version_minor;

  /* Read the setup data header and the vendor name string */
  const char *curr_data = additional_data;
  memcpy(&x_connection.setup_data.data, curr_data,
         sizeof(x_connection.setup_data.data));
  curr_data += sizeof(x_connection.setup_data.data);
  x_connection.setup_data.vendor_name =
      calloc(x_connection.setup_data.data.vendor_length + 1, 1);
  if (!x_connection.setup_data.vendor_name) {
    free(additional_data);
    x_disconnect();
    die("Memory allocation failed");
  }
  memcpy(x_connection.setup_data.vendor_name, curr_data,
         x_connection.setup_data.data.vendor_length);
  curr_data += x_connection.setup_data.data.vendor_length;

  /* Read the pixmap format data */
  x_connection.setup_data.pixmap_formats = calloc(
      x_connection.setup_data.data.num_pixmap_formats, sizeof(struct x_format));
  if (!x_connection.setup_data.pixmap_formats) {
    free(additional_data);
    x_disconnect();
    die("Memory allocation failed");
  }
  for (size_t i = 0; i < x_connection.setup_data.data.num_pixmap_formats; ++i) {
    memcpy(&x_connection.setup_data.pixmap_formats[i], curr_data,
           sizeof(struct x_format));
    curr_data += sizeof(struct x_format);
  }

  /* Read the screen data */
  x_connection.setup_data.roots =
      calloc(x_connection.setup_data.data.num_roots, sizeof(struct x_screen));
  if (!x_connection.setup_data.roots) {
    free(additional_data);
    x_disconnect();
    die("Memory allocation failed");
  }
  for (size_t i = 0; i < x_connection.setup_data.data.num_roots; ++i) {
    struct x_screen *screen = &x_connection.setup_data.roots[i];
    memcpy(&screen->data, curr_data, sizeof(struct x_screen_data));
    curr_data += sizeof(struct x_screen_data);
    /* Read allowed depths data */
    screen->allowed_depths =
        calloc(screen->data.num_allowed_depths, sizeof(struct x_depth));
    if (!screen->allowed_depths) {
      free(additional_data);
      x_disconnect();
      die("Memory allocation failed");
    }
    for (size_t j = 0; j < screen->data.num_allowed_depths; ++j) {
      struct x_depth *depth = &screen->allowed_depths[j];
      memcpy(&depth->data, curr_data, sizeof(struct x_depth_data));
      curr_data += sizeof(struct x_depth_data);
      /* Read visuals data */
      depth->visuals =
          calloc(depth->data.num_visuals, sizeof(struct x_visual_type));
      if (!depth->visuals) {
        free(additional_data);
        x_disconnect();
        die("Memory allocation failed");
      }
      for (size_t k = 0; k < depth->data.num_visuals; ++k) {
        memcpy(&depth->visuals[k], curr_data, sizeof(struct x_visual_type));
        curr_data += sizeof(struct x_visual_type);
      }
    }
  }
  free(additional_data);
}

static void parse_x_display_name(const char *full_name, char *hostname,
                                 size_t *hostname_len, unsigned long *number,
                                 unsigned long *screen) {
  /**
   * An X display string is of the form
   *     hostname:D.S
   * where:
   *  - hostname is the name of the computer where the X server is running. An
   *    omitted hostname means localhost.
   *  - D is a sequence number (usually zero). It can vary if there are multiple
   *    displays connected to a single computer.
   *  - S is the screen number. While a display can gave multiple screens, there
   *    is usually just one. Zero is the default value if S is omitted.
   *
   * In X parlance, a "display" is a collection of monitors linked to a common
   * input system (e.g., keyboard + mouse).
   *
   * Examples:
   *     localhost:2
   *     remote-server.com:0.0
   *     :0.1
   *
   * A name of the form hostname:D.S means screen S on display D on host
   * hostname. The X server for this display is listening on TCP port 6000+D.
   * A name of the form hostname/unix:D.S indicates that the connection to the X
   * server has to go through a UNIX socket located at /tmp/.X11-unix/X$D
   * instead of being a TCP connection.
   * :D.S is equivalent to localhost/unix:D.S.
   */
  const char *curr = full_name;
  while (*curr && *curr++ != ':')
    *hostname_len += 1;
  snprintf(hostname, *hostname_len, "%s", full_name);

  char *end = 0;
  errno = 0;
  *number = strtoul(curr, &end, 10);
  if (errno != 0)
    die("Invalid X display sequence number in display name");
  curr = end;
  *screen = strtoul(curr, 0, 10);
  if (errno != 0)
    die("Invalid X screen number in display name");
}

static int get_socket_for_display(const char *full_display_name) {
  char hostname[HOST_NAME_MAX] = {};
  size_t hostname_len = 0;
  unsigned long number = 0;
  unsigned long screen = 0;
  parse_x_display_name(full_display_name, hostname, &hostname_len, &number,
                       &screen);

  /* Check whether we are connecting through a UNIX domain socket. If so, it
   * must be to the local host. */
  int is_unix_connection = hostname_len == 0;
  if (hostname_len > 5 && hostname[hostname_len - 5] == '/' &&
      hostname[hostname_len - 4] == 'u' && hostname[hostname_len - 3] == 'n' &&
      hostname[hostname_len - 2] == 'i' && hostname[hostname_len - 1] == 'x')
    is_unix_connection = 1;

  int fd = -1;
  if (is_unix_connection) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
      die("Failed to create UNIX domain socket");

    struct sockaddr_un server_addr = {.sun_family = AF_UNIX};
    char path[sizeof(server_addr.sun_path)] = {};
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%lu", number);
    memcpy(server_addr.sun_path, path, sizeof(path));

    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1) {
      int errno_save = errno;
      close(fd);
      errno = errno_save;
      die("Failed to connect to X server");
    }
  } else {
    unsigned int port = X_BASE_TCP_PORT + number;
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG,
        .ai_protocol = 0,
    };
    struct addrinfo *info = 0;
    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%u", port);
    if (getaddrinfo(hostname, port_string, &hints, &info) != 0)
      die("Failed to resolve X server host name");

    /* Connect either via IPv4 or IPv6 depending on what is available and
     * configured */
    struct addrinfo *current_info = info;
    for (; current_info != 0; current_info = current_info->ai_next) {
      fd = socket(current_info->ai_family, current_info->ai_socktype,
                  current_info->ai_protocol);
      if (fd == -1)
        continue;
      if (connect(fd, current_info->ai_addr, current_info->ai_addrlen) != -1)
        break;
      /* Connection failed, close the socket and try the next one */
      close(fd);
    }
    int failed = current_info == 0;
    freeaddrinfo(info);
    if (failed)
      die("Failed to connect to X server");
  }
  return fd;
}

static void x_connect_to_display_with_auth_data(const char *full_display_name,
                                                size_t protocol_name_len,
                                                const char *protocol_name,
                                                size_t auth_data_len,
                                                const char *auth_data) {
  int fd = get_socket_for_display(full_display_name);
  x_connect_to_socket(fd, protocol_name_len, protocol_name, auth_data_len,
                      auth_data);
}

static int readu16be(FILE *file, uint16_t *result) {
  uint8_t res[2];
  if (fread(res, 1, 2, file) != 2)
    return 0;
  *result = res[0] * 256 + res[1];
  return 1;
}

static int read_counted_string(FILE *file, uint16_t *length, char **string) {
  if (!readu16be(file, length) || length == 0)
    return 0;
  *string = calloc(*length + 1, 1);
  if (!*string)
    die("Memory allocation failed");
  if (fread(*string, 1, *length, file) != *length) {
    free(*string);
    return 0;
  }
  return 1;
}

static void x_connect_to_display(const char *full_display_name) {
  char hostname[HOST_NAME_MAX] = {};
  size_t hostname_len = 0;
  unsigned long number = 0;
  unsigned long screen = 0;
  parse_x_display_name(full_display_name, hostname, &hostname_len, &number,
                       &screen);

  /* If there is no hostname in the display name then get the name of the local
   * host */
  if (hostname_len == 0)
    gethostname(hostname, HOST_NAME_MAX);

  /* Now that we have a valid host name, we can try to find the appropriate
   * authentication method to connect to the selected display. We read the
   * Xauthority file to determine which protocol and authentication data should
   * be used. */
  char *xauthority_path = getenv("XAUTHORITY");
  if (!xauthority_path) {
    /* If the path to the Xauthority file is not given in the environment, try
     * in the user's home directory */
    xauthority_path = "~/.Xauthority";
  }

  FILE *xauthority = fopen(xauthority_path, "rb");
  if (!xauthority)
    die("Failed to open Xauthority file");

  struct stat file_stat;
  fstat(fileno(xauthority), &file_stat);
  size_t file_size = file_stat.st_size;

  /* Go through the entire file and search for an entry that corresponds to our
   * target display */
  size_t auth_protocol_name_len = 0;
  char *auth_protocol_name = 0;
  size_t auth_data_len = 0;
  char *auth_data = 0;
  while (ftell(xauthority) != (long)file_size) {
    uint16_t family;
    uint16_t xauth_hostname_len;
    char *xauth_hostname;
    uint16_t xauth_number_len;
    char *xauth_number;
    uint16_t xauth_protocol_name_len;
    char *xauth_protocol_name;
    uint16_t xauth_data_len;
    char *xauth_data;
    if (readu16be(xauthority, &family) &&
        read_counted_string(xauthority, &xauth_hostname_len, &xauth_hostname) &&
        read_counted_string(xauthority, &xauth_number_len, &xauth_number) &&
        read_counted_string(xauthority, &xauth_protocol_name_len,
                            &xauth_protocol_name) &&
        read_counted_string(xauthority, &xauth_data_len, &xauth_data)) {
      if (strtoul(xauth_number, 0, 10) == number &&
          strcmp(hostname, xauth_hostname) == 0) {
        auth_protocol_name_len = xauth_protocol_name_len;
        auth_protocol_name = xauth_protocol_name;
        auth_data_len = xauth_data_len;
        auth_data = xauth_data;
      } else {
        free(xauth_protocol_name);
        /* Make some effort to avoid leaking secrets */
        explicit_bzero(xauth_data, xauth_data_len);
        free(xauth_data);
      }
      free(xauth_hostname);
      free(xauth_number);
    }
  }
  fclose(xauthority);

  if (!auth_data) {
    free(auth_protocol_name);
    die("No X authentication data");
  }

  x_connect_to_display_with_auth_data(full_display_name, auth_protocol_name_len,
                                      auth_protocol_name, auth_data_len,
                                      auth_data);
  free(auth_protocol_name);
  explicit_bzero(auth_data, auth_data_len);
  free(auth_data);
}

static const char *bool_to_string(int value) { return value ? "yes" : "no"; }

static void x_connect(void) {
  /* The DISPLAY environment variable contains the name of the display on which
   * the application was started */
  char *display_name = getenv("DISPLAY");
  if (!display_name) {
    fprintf(stderr, "WARNING: No DISPLAY environment variable found, trying "
                    "default X display name\n");
    display_name = ":0";
  }
  x_connect_to_display(display_name);
}

static unsigned int x_get_extension_opcode(const char *name) {
  size_t name_len = strlen(name);
  struct x_query_extension_request request = {
      .opcode = X_OPCODE_QUERY_EXTENSION,
      .request_len = 2 + (X_PAD(name_len) / 4),
      .name_len = name_len,
  };
  unsigned int opcode = 0;
  size_t request_len = sizeof(request) + X_PAD(name_len);
  char *request_buffer = malloc(request_len);
  if (!request_buffer)
    goto end;
  struct x_query_extension_reply reply = {};

  memcpy(request_buffer, &request, sizeof(request));
  memcpy(request_buffer + sizeof(request), name, name_len);

  ssize_t num_written = write_n(x_connection.fd, request_buffer, request_len);
  if (num_written != (long)request_len)
    goto end;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto end;

  opcode = reply.major_opcode;
end:
  free(request_buffer);
  return opcode;
}

static unsigned int enable_extension(const char *name) {
  unsigned int opcode = x_get_extension_opcode(name);
  if (strcmp(name, X_EXTENSION_NAME_BIG_REQUESTS) == 0)
    x_connection.big_requests_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_COMPOSITE) == 0)
    x_connection.composite_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_DAMAGE) == 0)
    x_connection.damage_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_DOUBLE_BUFFER) == 0)
    x_connection.double_buffer_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_DPMS) == 0)
    x_connection.dpms_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_DRI2) == 0)
    x_connection.dri2_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_DRI3) == 0)
    x_connection.dri3_opcode = opcode;
  else if (strcmp(name, X_EXTENSION_NAME_GLX) == 0)
    x_connection.glx_opcode = opcode;
  return opcode;
}

#define LEFT_PAD 0
#define FIELD_WIDTH 45

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
#define FILL "........................................"
#define SPACE "                                        "
#define PRINT_FIELD(name, format, ...)                                         \
  printf("%." STRINGIFY(LEFT_PAD) "s%." STRINGIFY(FIELD_WIDTH) "s " format     \
                                                               "\n",           \
         SPACE, name FILL, __VA_ARGS__)

static void print_x_connection_data(void) {
  const char *image_byte_order =
      x_connection.setup_data.data.image_byte_order ==
              X_BYTE_ORDER_LITTLE_ENDIAN
          ? "little endian"
          : "big endian";
  const char *bitmap_format_bit_order =
      x_connection.setup_data.data.bitmap_format_bit_order ==
              X_BITMAP_FORMAT_BIT_ORDER_LEAST_SIGNIFICANT
          ? "least significant"
          : "most significant";
  unsigned int release_major =
      x_connection.setup_data.data.release_number / 10000000;
  unsigned int release_minor =
      (x_connection.setup_data.data.release_number / 100000) % 100;
  unsigned int release_patch =
      (x_connection.setup_data.data.release_number / 1000) % 100;
  unsigned int release_build =
      x_connection.setup_data.data.release_number % 1000;

  PRINT_FIELD("Vendor", "%s", x_connection.setup_data.vendor_name);
  PRINT_FIELD("Version", "%u.%u", x_connection.setup_data.version_major,
              x_connection.setup_data.version_minor);
  if (release_build != 0)
    PRINT_FIELD("Release number", "%u.%u.%u.%u", release_major, release_minor,
                release_patch, release_build);
  else
    PRINT_FIELD("Release number", "%u.%u.%u", release_major, release_minor,
                release_patch);
  putc('\n', stdout);
  PRINT_FIELD("Resource ID base", "0x%08x",
              x_connection.setup_data.data.resource_id_base);
  PRINT_FIELD("Resource ID mask", "0x%08x",
              x_connection.setup_data.data.resource_id_mask);
  PRINT_FIELD("Motion buffer size", "%u",
              x_connection.setup_data.data.motion_buffer_size);
  PRINT_FIELD("Maximum request length", "%u bytes",
              4 * x_connection.setup_data.data.maximum_request_len);
  PRINT_FIELD("Image byte order", "%s", image_byte_order);
  PRINT_FIELD("Bitmap format bit order", "%s first", bitmap_format_bit_order);
  PRINT_FIELD("Bitmap format scanline unit", "%u",
              x_connection.setup_data.data.bitmap_format_scanline_unit);
  PRINT_FIELD("Bitmap format scanline pad", "%u",
              x_connection.setup_data.data.bitmap_format_scanline_pad);
  PRINT_FIELD("Max keycode", "%u", x_connection.setup_data.data.max_keycode);
  PRINT_FIELD("Min keycode", "%u", x_connection.setup_data.data.min_keycode);
  PRINT_FIELD("Number of pixmap formats", "%u",
              x_connection.setup_data.data.num_pixmap_formats);
  PRINT_FIELD("Number of screens", "%u",
              x_connection.setup_data.data.num_roots);

  printf("\nPixmap formats:\n");
  for (size_t i = 0; i < x_connection.setup_data.data.num_pixmap_formats; ++i) {
    const struct x_format *format = &x_connection.setup_data.pixmap_formats[i];
    printf("  * depth = %2u, bits per pixel = %2u, scanline pad = %u\n",
           format->depth, format->bits_per_pixel, format->scanline_pad);
  }

  printf("\nScreens:\n");
  for (size_t i = 0; i < x_connection.setup_data.data.num_roots; ++i) {
    const struct x_screen *screen = &x_connection.setup_data.roots[i];
    printf("  Screen #%zu\n", i);
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
    const char *backing_stores =
        screen->data.backing_stores == X_BACKING_STORES_NEVER
            ? "never"
            : (screen->data.backing_stores == X_BACKING_STORES_WHEN_MAPPED
                   ? "when mapped"
                   : "always");

    PRINT_FIELD("Root", "0x%08x", screen->data.root);
    PRINT_FIELD("Default colormap", "0x%08x", screen->data.default_colormap);
    PRINT_FIELD("White pixel", "0x%08x", screen->data.white_pixel);
    PRINT_FIELD("Black pixel", "0x%08x", screen->data.black_pixel);
    /* TODO: Pretty-print this field */
    PRINT_FIELD("Current input mask", "0x%08x",
                screen->data.current_input_mask);
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 6
#define FIELD_WIDTH 39
    PRINT_FIELD("Key press", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_KEY_PRESS));
    PRINT_FIELD("Key release", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_KEY_RELEASE));
    PRINT_FIELD("Button press", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON_PRESS));
    PRINT_FIELD("Button release", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON_RELEASE));
    PRINT_FIELD("Enter window", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_ENTER_WINDOW));
    PRINT_FIELD("Leave window", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_LEAVE_WINDOW));
    PRINT_FIELD("Pointer motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_POINTER_MOTION));
    PRINT_FIELD("Pointer motion hint", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_POINTER_MOTION_HINT));
    PRINT_FIELD("Button 1 motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON1_MOTION));
    PRINT_FIELD("Button 2 motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON2_MOTION));
    PRINT_FIELD("Button 3 motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON3_MOTION));
    PRINT_FIELD("Button 4 motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON4_MOTION));
    PRINT_FIELD("Button 5 motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON5_MOTION));
    PRINT_FIELD("Button motion", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_BUTTON_MOTION));
    PRINT_FIELD("Keymap state", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_KEYMAP_STATE));
    PRINT_FIELD("Exposure", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_EXPOSURE));
    PRINT_FIELD("Visibility change", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_VISIBILITY_CHANGE));
    PRINT_FIELD("Structure notify", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_STRUCTURE_NOTIFY));
    PRINT_FIELD("Resize redirect", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_RESIZE_REDIRECT));
    PRINT_FIELD("Substructure notify", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_SUBSTRUCTURE_NOTIFY));
    PRINT_FIELD("Substructure redirect", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_SUBSTRUCTURE_REDIRECT));
    PRINT_FIELD("Focus change", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_FOCUS_CHANGE));
    PRINT_FIELD("Property change", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_PROPERTY_CHANGE));
    PRINT_FIELD("Colormap change", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_COLORMAP_CHANGE));
    PRINT_FIELD("Owner grab button", "%s",
                bool_to_string(screen->data.current_input_mask &
                               X_EVENT_MASK_OWNER_GRAB_BUTTON));

#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
    PRINT_FIELD("Size", "%ux%u pixels (%ux%u mm)", screen->data.width_in_pixels,
                screen->data.height_in_pixels,
                screen->data.width_in_millimeters,
                screen->data.height_in_millimeters);
    PRINT_FIELD("Installed maps", "min = %u, max = %u",
                screen->data.min_installed_maps,
                screen->data.max_installed_maps);
    PRINT_FIELD("Root visual id", "0x%08x", screen->data.root_visual_id);
    PRINT_FIELD("Backing stores", "%s", backing_stores);
    PRINT_FIELD("Save unders", "%s", bool_to_string(screen->data.save_unders));
    PRINT_FIELD("Root depth", "%u", screen->data.root_depth);
    PRINT_FIELD("Number of allowed depths", "%u",
                screen->data.num_allowed_depths);

    printf("    Allowed depths:\n");
    for (size_t j = 0; j < screen->data.num_allowed_depths; ++j)
      printf("      * depth = %2u, number of visuals: %u\n",
             screen->allowed_depths[j].data.depth,
             screen->allowed_depths[j].data.num_visuals);
  }
}

static void print_x_font_path(void) {
  struct x_get_font_path_request request = {
      .opcode = X_OPCODE_GET_FONT_PATH,
      .request_len = sizeof(struct x_get_font_path_request) / 4,
  };
  char *additional_data = 0;

  /* Request the font search paths from the X server */
  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto font_error;

  struct x_get_font_path_reply reply = {};
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  /* If we fail, don't try to understand why and just return */
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto font_error;

  size_t additional_data_len = 4 * reply.data_len;
  additional_data = malloc(additional_data_len);
  if (!additional_data)
    goto font_error;
  num_read = read(x_connection.fd, additional_data, additional_data_len);
  if (num_read != (ssize_t)additional_data_len)
    goto font_error;

  printf("\nFont search paths:\n");
  char path[256] = {};
  const char *curr_data = additional_data;
  for (size_t i = 0; i < reply.num_strings; ++i) {
    uint8_t path_len = 0;
    memcpy(&path_len, curr_data, 1);
    curr_data += 1;
    memcpy(path, curr_data, path_len);
    path[path_len] = '\0';
    curr_data += path_len;
    printf("  * %s\n", path);
  }

  free(additional_data);
  return;

font_error:
  free(additional_data);
  fprintf(stderr, "ERROR: Failed get X font search paths\n");
}

static int string_comparator(const void *lhs, const void *rhs) {
  const char **first = (const char **)lhs;
  const char **second = (const char **)rhs;
  return strcmp(*first, *second);
}

static void print_x_extensions(void) {
  struct x_list_extensions_request request = {
      .opcode = X_OPCODE_LIST_EXTENSIONS,
      .request_len = sizeof(struct x_list_extensions_request) / 4,
  };
  struct x_list_extensions_reply reply = {};
  char *additional_data = 0;
  char **extension_names = 0;

  /* Request the list of supported extensions from the X server */
  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto extensions_error;

  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  /* If we fail, don't try to understand why and just return */
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto extensions_error;

  size_t additional_data_len = 4 * reply.data_len;
  additional_data = malloc(additional_data_len);
  if (!additional_data)
    goto extensions_error;
  num_read = read(x_connection.fd, additional_data, additional_data_len);
  if (num_read != (ssize_t)additional_data_len)
    goto extensions_error;

  extension_names = calloc(reply.num_names, sizeof(char *));
  if (!extension_names)
    goto extensions_error;
  const char *curr_data = additional_data;
  for (size_t i = 0; i < reply.num_names; ++i) {
    uint8_t name_len = 0;
    memcpy(&name_len, curr_data, 1);
    curr_data += 1;
    extension_names[i] = calloc(name_len + 1, 1);
    if (!extension_names[i])
      goto extensions_error;
    memcpy(extension_names[i], curr_data, name_len);
    extension_names[i][name_len] = '\0';
    curr_data += name_len;
  }

  qsort(extension_names, reply.num_names, sizeof(char *), string_comparator);
  printf("\nSupported extensions: %u\n", reply.num_names);
  for (size_t i = 0; i < reply.num_names; ++i) {
    unsigned int opcode = enable_extension(extension_names[i]);
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
    if (opcode)
      printf("  * %s%.*s %u\n", extension_names[i],
             (int)(FIELD_WIDTH - strlen(extension_names[i])), FILL, opcode);
  }

  for (size_t i = 0; i < reply.num_names; ++i)
    free(extension_names[i]);
  free(extension_names);
  free(additional_data);
  return;

extensions_error:
  if (extension_names) {
    for (size_t i = 0; i < reply.num_names; ++i)
      free(extension_names[i]);
  }
  free(extension_names);
  free(additional_data);
  fprintf(stderr, "ERROR: Failed to query supported X extensions");
}

static void print_big_requests_info(void) {
  struct x_big_requests_enable_request request = {
      .opcode = x_connection.big_requests_opcode,
      .extension_opcode = X_OPCODE_BIG_REQUESTS_ENABLE,
      .request_len = sizeof(struct x_big_requests_enable_request) / 4,
  };
  struct x_big_requests_enable_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_BIG_REQUESTS ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Maximum request length", "%zu bytes",
              4 * (uint64_t)reply.max_request_len);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_BIG_REQUESTS
                  " extension information\n");
  return;
}

static void print_composite_info(void) {
  struct x_composite_query_version_request request = {
      .opcode = x_connection.composite_opcode,
      .extension_opcode = X_OPCODE_COMPOSITE_QUERY_VERSION,
      .request_len = sizeof(struct x_composite_query_version_request) / 4,
      .version_major = (uint32_t)-1,
      .version_minor = (uint32_t)-1,
  };
  struct x_composite_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_COMPOSITE ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_COMPOSITE
                  " extension information\n");
  return;
}

static void print_damage_info(void) {
  struct x_damage_query_version_request request = {
      .opcode = x_connection.damage_opcode,
      .extension_opcode = X_OPCODE_DAMAGE_QUERY_VERSION,
      .request_len = sizeof(struct x_damage_query_version_request) / 4,
      .version_major = (uint32_t)-1,
      .version_minor = (uint32_t)-1,
  };
  struct x_damage_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_DAMAGE ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_DAMAGE
                  " extension information\n");
  return;
}

static void print_double_buffer_info(void) {
  struct x_double_buffer_get_version_request request = {
      .opcode = x_connection.double_buffer_opcode,
      .extension_opcode = X_OPCODE_DOUBLE_BUFFER_GET_VERSION,
      .request_len = sizeof(struct x_double_buffer_get_version_request) / 4,
      .version_major = (uint8_t)-1,
      .version_minor = (uint8_t)-1,
  };
  struct x_damage_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_DOUBLE_BUFFER ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_DOUBLE_BUFFER
                  " extension information\n");
  return;
}

static void print_dpms_info(void) {
  struct x_dpms_get_version_request request = {
      .opcode = x_connection.double_buffer_opcode,
      .extension_opcode = X_OPCODE_DPMS_GET_VERSION,
      .request_len = sizeof(struct x_dpms_get_version_request) / 4,
      .version_major = (uint16_t)-1,
      .version_minor = (uint16_t)-1,
  };
  struct x_dpms_get_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_DPMS " (Display Power Management Signaling):\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  /* Check whether the current graphics card/monitor combination is capable of
   * using DPMS */
  struct x_dpms_capable_request dpms_capable_request = {
      .opcode = x_connection.dpms_opcode,
      .extension_opcode = X_OPCODE_DPMS_CAPABLE,
      .request_len = sizeof(struct x_dpms_capable_request) / 4,
  };
  struct x_dpms_capable_reply dpms_capable_reply = {};
  num_written = write_n(x_connection.fd, &dpms_capable_request,
                        sizeof(dpms_capable_request));
  if (num_written != sizeof(dpms_capable_request))
    goto error;
  num_read =
      read_n(x_connection.fd, &dpms_capable_reply, sizeof(dpms_capable_reply));
  if (num_read != sizeof(dpms_capable_reply))
    goto error;
  else if (dpms_capable_reply.status == X_REPLY) {
    PRINT_FIELD("DPMS capable", "%s",
                bool_to_string(dpms_capable_reply.capable));
  }

  /* Retrieve DPMS timeout information */
  struct x_dpms_get_timeouts_request dpms_get_timeouts_request = {
      .opcode = x_connection.dpms_opcode,
      .extension_opcode = X_OPCODE_DPMS_GET_TIMEOUTS,
      .request_len = sizeof(struct x_dpms_get_timeouts_request) / 4,
  };
  struct x_dpms_get_timeouts_reply dpms_get_timeouts_reply = {};
  num_written = write_n(x_connection.fd, &dpms_get_timeouts_request,
                        sizeof(dpms_get_timeouts_request));
  if (num_written != sizeof(dpms_get_timeouts_request))
    goto error;
  num_read = read_n(x_connection.fd, &dpms_get_timeouts_reply,
                    sizeof(dpms_get_timeouts_reply));
  if (num_read != sizeof(dpms_get_timeouts_reply))
    goto error;
  else if (dpms_get_timeouts_reply.status == X_REPLY) {
    if (dpms_get_timeouts_reply.standby_timeout)
      PRINT_FIELD("Standby timeout", "%u seconds",
                  dpms_get_timeouts_reply.standby_timeout);
    else
      PRINT_FIELD("Standby mode", "%s", "disabled");
    if (dpms_get_timeouts_reply.suspend_timeout)
      PRINT_FIELD("Suspend timeout", "%u seconds",
                  dpms_get_timeouts_reply.suspend_timeout);
    else
      PRINT_FIELD("Suspend screen", "%s", "disabled");
    if (dpms_get_timeouts_reply.off_timeout)
      PRINT_FIELD("Power-off timeout", "%u seconds",
                  dpms_get_timeouts_reply.off_timeout);
    else
      PRINT_FIELD("Power-off screen", "%s", "disabled");
  }

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_DPMS
                  " extension information\n");
  return;
}

static void print_dri2_info(void) {
  struct x_dri2_query_version_request request = {
      .opcode = x_connection.dri2_opcode,
      .extension_opcode = X_OPCODE_DRI2_QUERY_VERSION,
      .request_len = sizeof(struct x_dri2_query_version_request) / 4,
      .version_major = (uint32_t)-1,
      .version_minor = (uint32_t)-1,
  };
  struct x_dri2_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_DRI2 ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_DRI2
                  " extension information\n");
  return;
}

static void print_dri3_info(void) {
  struct x_dri3_query_version_request request = {
      .opcode = x_connection.dri3_opcode,
      .extension_opcode = X_OPCODE_DRI3_QUERY_VERSION,
      .request_len = sizeof(struct x_dri3_query_version_request) / 4,
      .version_major = (uint32_t)-1,
      .version_minor = (uint32_t)-1,
  };
  struct x_dri3_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_DRI3 ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_DRI3
                  " extension information\n");
  return;
}

static void print_glx_info(void) {
  struct x_glx_query_version_request request = {
      .opcode = x_connection.glx_opcode,
      .extension_opcode = X_OPCODE_GLX_QUERY_VERSION,
      .request_len = sizeof(struct x_glx_query_version_request) / 4,
      .version_major = (uint32_t)-1,
      .version_minor = (uint32_t)-1,
  };
  struct x_glx_query_version_reply reply = {};

  ssize_t num_written = write_n(x_connection.fd, &request, sizeof(request));
  if (num_written != sizeof(request))
    goto error;
  ssize_t num_read = read_n(x_connection.fd, &reply, sizeof(reply));
  if (num_read != sizeof(reply) || reply.status != X_REPLY)
    goto error;

  printf("  " X_EXTENSION_NAME_GLX ":\n");
#undef LEFT_PAD
#undef FIELD_WIDTH
#define LEFT_PAD 4
#define FIELD_WIDTH 41
  PRINT_FIELD("Latest supported version", "%u.%u", reply.version_major,
              reply.version_minor);

  return;
error:
  fprintf(stderr, "ERROR: Failed to get " X_EXTENSION_NAME_GLX
                  " extension information\n");
  return;
}

static void print_x_extensions_info(void) {
  printf("\nExtensions information:\n");
  if (x_connection.big_requests_opcode)
    print_big_requests_info();
  if (x_connection.composite_opcode)
    print_composite_info();
  if (x_connection.damage_opcode)
    print_damage_info();
  if (x_connection.double_buffer_opcode)
    print_double_buffer_info();
  if (x_connection.dpms_opcode)
    print_dpms_info();
  if (x_connection.dri2_opcode)
    print_dri2_info();
  if (x_connection.dri3_opcode)
    print_dri3_info();
  if (x_connection.glx_opcode)
    print_glx_info();
}

int main() {
  printf("xinfo - X server information printer\n\n");

  x_connect();
  print_x_connection_data();
  print_x_font_path();
  print_x_extensions();
  print_x_extensions_info();
  x_disconnect();
  return 0;
}
