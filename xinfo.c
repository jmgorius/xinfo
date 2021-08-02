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
  fprintf(stderr, "ERROR: %s\n", msg);
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

#define X_CONNECTION_STATUS_SUCCESS 1
#define X_BYTE_ORDER_LITTLE_ENDIAN 0
#define X_BITMAP_FORMAT_BIT_ORDER_LEAST_SIGNIFICANT 0
#define X_BACKING_STORES_NEVER 0
#define X_BACKING_STORES_WHEN_MAPPED 1

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
  uint16_t maximum_request_length;
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
} x_connection = {
    .fd = -1,
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
      calloc(x_connection.setup_data.data.vendor_length, 1);
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

static void dump_x_connection_data(void) {
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
              x_connection.setup_data.data.maximum_request_length);
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
    PRINT_FIELD("Size", "%ux%u pixels (%ux%u mm)", screen->data.width_in_pixels,
                screen->data.height_in_pixels,
                screen->data.width_in_millimeters,
                screen->data.height_in_millimeters);
    PRINT_FIELD("Installed maps", "min = %u, max = %u",
                screen->data.min_installed_maps,
                screen->data.max_installed_maps);
    PRINT_FIELD("Root visual id", "0x%08x", screen->data.root_visual_id);
    PRINT_FIELD("Backing stores", "%s", backing_stores);
    PRINT_FIELD("Save unders", "%s", screen->data.save_unders ? "yes" : "no");
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

int main() {
  printf("xinfo - X server information dumper\n\n");
  x_connect();

  dump_x_connection_data();

  x_disconnect();
  return 0;
}
