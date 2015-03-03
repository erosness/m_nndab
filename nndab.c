/// a basic dab communicator that uses a binary nanomsg reqrep socket.
/// Adellica 2015

#include <stdio.h>
#include <fcntl.h>
#include <assert.h>

#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

// taken from linux/i2c-dev.h (not present in android-build system)
#define I2C_SLAVE 0x0703

#define VENICE_I2C_DEVICE "/dev/i2c-2"
#define VENICE_I2C_ADDR 0x75

// memory available for a dab packet. if to small, we enter "deadlock"
// where we can't flush the i2c packet queue.
#define DAB_PACKET_MAX 4096

struct mybuff {
  // need a pointer to original malloc so we can free() if we want
  // dynamic memory.
  char* data;
  int len;
};

void hex_print(char* buff, int len) {
  int i;
  for( i = 0 ; i < len ; i++)
    fprintf(stderr, "%02X ", buff[i]);
  fprintf(stderr, "\n");
}

int dab_set_length_from_i2c(struct mybuff* buff) {
  buff->len = ((((unsigned char)buff->data[0]) << 8) |
               (((unsigned char)buff->data[1]) << 0));
  buff->data = &buff->data[2];

  //fprintf(stderr, "debug: actual pauyload is %d bytes\n", buff->len);
  return 0;
}

// read data into buff (up to max bytes) from fd. try several times in
// case the DAB is being slow and silly.
//
// this part of the i2c protocol is tricky. we read 2 bytes first to
// get the size of the next packet. it won't be consumed, so the first
// fd read (of 2 bytes) is like a peek. only if you read the entire
// packet payload will it be consumed by the i2c driver's buffer. we
// could just read everything in a 8k slurp but that would make it
// slow - this enables us to read exactly as many bytes as is needed
// (except reading the size header twice)..
int read_dab_packet(int fd, struct mybuff* buff) {
  char size[2];

  int len = read(fd, size, 2);
  if(len != 2) {
    fprintf(stderr, "error: io ac564230315f\n");
    return 11;
  }

  int packet_size =
    (((unsigned char*)size)[0] << 8) +
    (((unsigned char*)size)[1] << 0);

  if(packet_size <= 0) {
    return -EAGAIN;
  }

  int to_read = packet_size + 2;
  if(buff->len < to_read) {
    fprintf(stderr, "dab packet buffer not large enough to flush packet!\n");
    to_read = buff->len;
  }

  fprintf(stderr, "venice packet size is %d, to read %d\n", packet_size, to_read);

  // read up to to_read bytes
  len = read(fd, buff->data, to_read);
  fprintf(stderr, "venice read fd hex:");
  hex_print(buff->data, to_read);

  // we always expect to get requested number bytes back. the actual
  // payload length is stored in 2 first bytes of data. it's very
  // silly, I know.
  if(len != to_read) {
    fprintf(stderr, "error: io error, len ≠ packet_size %d\n", len);
    buff->data = "error b398993f07ab len ≠ packet_size";
    buff->len = strlen(buff->data);
    return 22;
  } else {
    // successful dab packet. double-check length header is same as
    // before:
    if(packet_size !=
       (((unsigned char*)buff->data)[0] << 8) +
       (((unsigned char*)buff->data)[1] << 0)) {
      fprintf(stderr, "error 396c73005fe1 packet size mismatch");
      buff->data = "error 1175a1c05466 packet size mismatch";
      buff->len = strlen(buff->data);
      return 33; // TODO better error code here?
    }

    // we have a good dab packet! remove 2-byte size header
    buff->data = &buff->data[2];
    buff->len = packet_size;
    return 0;
  }
}

void dab_flush(int fd) {
  char _data[1024];
  // note that we need content here
  struct mybuff buff = {.len = 1024, .data = _data};
  while(read_dab_packet(fd, &buff) != -EAGAIN) { };
}

int main(int argc, char **argv) {
  int err;
  int socket = nn_socket(AF_SP, NN_REP);
  nn_bind(socket, "ipc:///cache/nndab");
  nn_bind(socket, "tcp://0.0.0.0:12000");

  // ==================== setup i2c ====================
  int venice_fd = open(VENICE_I2C_DEVICE, O_RDWR);
  if(venice_fd == -1) {
    perror(VENICE_I2C_DEVICE);
    return 1;
  }
  if (ioctl(venice_fd, I2C_SLAVE, VENICE_I2C_ADDR) == -1) {
    perror(VENICE_I2C_DEVICE);
    return 2;
  }

  // container for data coming from venice over i2c
  char _dab_data[DAB_PACKET_MAX];

  // static nnbuff so we can prepend 2-byte size header
  char nnbuf[2048];

  while (1) {
    struct mybuff _dbuff = {.len = DAB_PACKET_MAX , .data = _dab_data}; // dab buffer
    struct mybuff *buff = &_dbuff; // convenience pointer (so it's all -> and no .)


    // nice and hacky for prepending the 2-byte size header
    int bytes = nn_recv(socket, &nnbuf[2], 2048 - 2, 0);
    assert(bytes >= 0);

    fprintf(stderr, "incoming %d nnbytes\n", bytes);

    // prepend length for DAB i2c packet
    ((unsigned char*)nnbuf)[0] = ((bytes >> 8) & 0xFF);
    ((unsigned char*)nnbuf)[1] = ((bytes >> 0) & 0xFF);
    bytes += 2;

    // simple data transfer nnsocket => venice_fd
    fprintf(stderr, "write hex:");
    hex_print(nnbuf, bytes);

    dab_flush(venice_fd);

    if((err = write(venice_fd, nnbuf, bytes)) != bytes) {
      fprintf(stderr, "error: could not send %d bytes: %d\n", bytes, err);
      buff->data = "error 676b897e8412";
      buff->len = strlen(buff->data);
    } else {
      // written ok, read response: yeah, really, 10000 retries. it
      // typically takes about 500 retries after (dab.state 'on)
      // command so I wanna be on the safe side but still avoid
      // infinite loop.
      int ret, retries = 10000;
      while(retries > 0 && (ret = read_dab_packet(venice_fd, buff)) == -EAGAIN) {
        fprintf(stderr, "retry %d empty packet after write, retrying\n", retries);
        retries--;
      }

      if(ret != 0) {
        fprintf(stderr, "error 3e923fcac076 could not read dab packet\n");
        buff->data = "error: e0f172e4bf58 could not read dab packet";
        buff->len = strlen(buff->data);
      } // else buff is properly set up
    }
    fprintf(stderr, "replying with %d bytes\n", buff->len);
    nn_send(socket, buff->data, buff->len, 0);

    // no nn_freemsg because we're static
  }
  return 0;
}
