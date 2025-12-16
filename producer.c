/*
 *  V4L2 video output example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <time.h> /* clock_gettime() */
#include <getopt.h> /* getopt_long() */

#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <libgen.h>

#include "common.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define SET_QUEUED(buffer) ((buffer).flags |= V4L2_BUF_FLAG_QUEUED)

#define IS_QUEUED(buffer) \
	((buffer).flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))

enum io_method {
	IO_METHOD_WRITE,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
};

struct buffer {
	void *start;
	size_t length;
	size_t bytesused;
};

static char *dev_name;
static enum io_method io = IO_METHOD_MMAP;
static int fd = -1;
struct buffer *buffers;
static unsigned int n_buffers;
static int frame_count = 70;
static unsigned int width = 640;
static unsigned int height = 480;
static unsigned int pixelformat = V4L2_PIX_FMT_YUYV;
static int set_timestamp = 0;
static char strbuf[1024];

static void errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}
static unsigned int str2fourcc(char buf[4])
{
	return (buf[0]) + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
}

static int xioctl(int fh, unsigned long int request, void *arg)
{
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

#if 0
static unsigned int random_nextseed = 148985372;
static unsigned char randombyte(void)
{
	random_nextseed = (random_nextseed * 472940017) + 832416023;
	return ((random_nextseed >> 16) & 0xFF);
}
#endif


/* jonah modify */
static char *input_filename = NULL;
static FILE *input_file = NULL;
static size_t frame_size = 0;
static int loop_count = 0;
static size_t file_size = 0;
static unsigned int bytes_per_line = 0;
static unsigned int size_image = 0;
static int target_fps = 30;
static unsigned int frame_interval_us = 33333;

static void process_image(unsigned char *data, size_t length)
{
  #if 0
	size_t i;
	for (i = 0; i < length; i++) {
		data[i] = randombyte();
	}
	#else
	if ( input_file) {
	  struct timespec ts_start, ts_end;
	  clock_gettime(CLOCK_MONOTONIC, &ts_start);
	  size_t total_read = 0;
	  while (total_read < length) {
	    size_t chunk = fread(data + total_read, 1, length - total_read, input_file);
	    if (chunk == 0) {
	      if (feof(input_file)) {
		if (loop_count != 0 ){
		  loop_count--;
		  if (loop_count == 0){
		    memset(data + total_read, 0, length - total_read);
		    return;
		  }
		}
		rewind(input_file);
		clearerr(input_file);
		continue;
	      } else {
		break;
	      }
	    }
	    total_read += chunk;
	  }
	  if (total_read < length) {
	    memset(data + total_read, 0, length - total_read);
	  }
	  clock_gettime(CLOCK_MONOTONIC, &ts_end);
	  long long elapsed_ns = (long long)(ts_end.tv_sec - ts_start.tv_sec) * 1000000000LL +
	                         (long long)(ts_end.tv_nsec - ts_start.tv_nsec);
	  double elapsed_ms = (double)elapsed_ns / 1000000.0;
	  double mbps = (elapsed_ms > 0.0)
	    ? ((double)total_read / (1024.0 * 1024.0)) / (elapsed_ms / 1000.0)
	    : 0.0;
	  printf("fread perf: bytes=%zu time=%.3f ms speed=%.2f MB/s\n",
	         total_read, elapsed_ms, mbps);
	}
	#endif
}


// jonha add open file function
static void open_input_file(void){
  if (!input_filename){
    return;
  }

  input_file = fopen(input_filename, "rb");
  if (!input_file){
    fprintf(stderr, "Cannot open input file '%s' : %s\n",
	    input_filename, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // get file seek
  fseek(input_file, 0, SEEK_END);
  file_size = (size_t)ftell(input_file);
  rewind(input_file);
  printf("Opened input file: %s (size: %zu bytes)\n",
	 input_filename, file_size);

  if (size_image != 0){
    frame_size = (size_t)size_image;
  } else if (bytes_per_line != 0) {
    frame_size = (size_t)bytes_per_line * height;
  } else {
    frame_size = (size_t)width * height * 2;
  }
  if (frame_size > 0) {
    printf("Frame size: %zu bytes, estimated frames in file: %zu \n",
	   frame_size, file_size / frame_size);
  } else {
    printf("Frame size is zero; check pixel format/width/height.\n");
  }
}

//static int framenum = 0;
static int write_frame(void)
{
	struct v4l2_buffer buf;
	unsigned int i;

	switch (io) {
	case IO_METHOD_WRITE:
		process_image(buffers[0].start, buffers[0].bytesused);
		if (-1 == write(fd, buffers[0].start, buffers[0].length)) {
			switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				errno_exit("write");
			}
		}
		printf("WRITE %p: %lu/%lu\n", buffers[0].start,
		       buffers[0].bytesused, buffers[0].length);
		break;

	case IO_METHOD_MMAP:
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
			switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				errno_exit("VIDIOC_DQBUF");
			}
		}
		if (set_timestamp) {
			struct timespec curTime;
			clock_gettime(CLOCK_MONOTONIC, &curTime);
			buf.timestamp.tv_sec = curTime.tv_sec;
			buf.timestamp.tv_usec = curTime.tv_nsec / 1000ULL;
		} else {
			buf.timestamp.tv_sec = 0;
			buf.timestamp.tv_usec = 0;
		}
		printf("MMAP\t%s\n",
		       snprintf_buffer(strbuf, sizeof(strbuf), &buf));
		fflush(stdout);
		assert(buf.index < n_buffers);
		if (frame_size == 0 || frame_size > buffers[buf.index].length) {
			buf.bytesused = buffers[buf.index].length;
		} else {
			buf.bytesused = (unsigned int)frame_size;
		}
		process_image(buffers[buf.index].start, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		if (!IS_QUEUED(buf)) {
			printf("driver pretends buffer is not queued even if queue succeeded\n");
			SET_QUEUED(buf);
		}
		break;

	case IO_METHOD_USERPTR:
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory = V4L2_MEMORY_USERPTR;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
			switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				errno_exit("VIDIOC_DQBUF");
			}
		}

		for (i = 0; i < n_buffers; ++i)
			if (buf.m.userptr == (unsigned long)buffers[i].start &&
			    buf.bytesused == buffers[i].bytesused)
				break;

		assert(i < n_buffers);
		printf("USERPTR\t%s\n",
		       snprintf_buffer(strbuf, sizeof(strbuf), &buf));
		if (frame_size == 0 || frame_size > buffers[buf.index].length) {
			buf.bytesused = buffers[buf.index].length;
		} else {
			buf.bytesused = (unsigned int)frame_size;
		}
		process_image(buffers[buf.index].start, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		break;
	}

	return 1;
}

static void mainloop(void)
{
	unsigned int count;
	//int keep_running = 1;

	count = frame_count;

	while (1) {
		struct timespec frame_start, frame_end;
		long long elapsed_us, sleep_us;
		clock_gettime(CLOCK_MONOTONIC, &frame_start);
		if (count < 1)
			break;
		if (frame_count >= 0) {
			count--;
		}

		for (;;) {
			if (write_frame())
				break;
			/* EAGAIN - continue select loop. */
		}
		clock_gettime(CLOCK_MONOTONIC, &frame_end);
		elapsed_us = (long long)(frame_end.tv_sec - frame_start.tv_sec) * 1000000LL +
			     (long long)(frame_end.tv_nsec - frame_start.tv_nsec) / 1000LL;
		sleep_us = (long long)frame_interval_us - elapsed_us;
		if (sleep_us > 0) {
			usleep((useconds_t)sleep_us);
		}
	}
}

static void stop_capturing(void)
{
	enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_WRITE:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
			errno_exit("VIDIOC_STREAMOFF");
		break;
	}
}

static void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_WRITE:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;
			buf.length = buffers[i].length;
			buf.bytesused = buffers[i].bytesused;

			printf("MMAP init qbuf %d/%d (length=%zu): %s\n", i,
			       n_buffers, buffers[i].length,
			       snprintf_buffer(strbuf, sizeof(strbuf), &buf));
			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
		}
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
		break;

	case IO_METHOD_USERPTR:
		for (i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			buf.memory = V4L2_MEMORY_USERPTR;
			buf.index = i;
			buf.m.userptr = (unsigned long)buffers[i].start;
			buf.bytesused = buffers[i].bytesused;
			buf.length = buffers[i].length;

			printf("USERPTR init qbuf %d/%d: %s\n", i, n_buffers,
			       snprintf_buffer(strbuf, sizeof(strbuf), &buf));
			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
		}
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
		break;
	}
}

static void uninit_device(void)
{
	unsigned int i;

	switch (io) {
	case IO_METHOD_WRITE:
		free(buffers[0].start);
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i)
			if (-1 == munmap(buffers[i].start, buffers[i].length))
				errno_exit("munmap");
		break;

	case IO_METHOD_USERPTR:
		for (i = 0; i < n_buffers; ++i)
			free(buffers[i].start);
		break;
	}

	free(buffers);
}

static void init_write(unsigned int buffer_size)
{
	buffers = calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	buffers[0].length = buffer_size;
	buffers[0].bytesused = buffer_size;
	buffers[0].start = malloc(buffer_size);

	if (!buffers[0].start) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
}

static void init_mmap(void)
{
	const int count = 4;
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count = count;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr,
				"%s does not support "
				"memory mapping\n",
				dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}
	printf("requested %d buffers, got %d\n", count, req.count);

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");
		printf("requested buffer %d/%d: %s\n", n_buffers, count,
		       snprintf_buffer(strbuf, sizeof(strbuf), &buf));

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].bytesused = buf.bytesused;
		buffers[n_buffers].start =
			mmap(NULL /* start anywhere */, buf.length,
			     PROT_READ | PROT_WRITE /* required */,
			     MAP_SHARED /* recommended */, fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
		printf("buffer#%d @%p of %zu bytes\n", n_buffers,
		       buffers[n_buffers].start, buffers[n_buffers].length);
	}
}

static void init_userp(unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr,
				"%s does not support "
				"user pointer i/o\n",
				dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	buffers = calloc(4, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = malloc(buffer_size);
		buffers[n_buffers].bytesused = buffer_size;

		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

static void init_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
		fprintf(stderr, "%s is no video output device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	switch (io) {
	case IO_METHOD_WRITE:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			fprintf(stderr, "%s does not support write i/o\n",
				dev_name);
			exit(EXIT_FAILURE);
		}
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "%s does not support streaming i/o\n",
				dev_name);
			exit(EXIT_FAILURE);
		}
		break;
	}

	/* Select video input, video standard and tune here. */

	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		crop.c = cropcap.defrect; /* reset to default */

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
			case EINVAL:
				/* Cropping not supported. */
				break;
			default:
				/* Errors ignored. */
				break;
			}
		}
	} else {
		/* Errors ignored. */
	}

	CLEAR(fmt);

	/* get the current format */
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
		errno_exit("VIDIOC_G_FMT");
	printf("get format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));

	/* try to set the current format (no-change should always succeed) */
	if (xioctl(fd, VIDIOC_TRY_FMT, &fmt) < 0)
		errno_exit("VIDIOC_TRY_FMT");
	printf("tried format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));
	/* and get the format again */
	if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
		errno_exit("VIDIOC_G_FMT");
	printf("got format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));

	/* try to set the current format (no-change should always succeed) */
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
		errno_exit("VIDIOC_S_FMT");
	printf("set format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));

	switch (fmt.type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		fmt.fmt.pix.width = width;
		fmt.fmt.pix.height = height;
		fmt.fmt.pix.pixelformat = pixelformat;
		break;
	default:
		printf("unable to set format for anything but output/single-plane\n");
		break;
	}
	printf("finalizing format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		fprintf(stderr, "VIDIOC_S_FMT error %d, %s\n", errno,
			strerror(errno));
	}
	printf("final format: %s\n",
	       snprintf_format(strbuf, sizeof(strbuf), &fmt));
	/* cache alignment-relevant fields */
	switch (fmt.type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		bytes_per_line = fmt.fmt.pix.bytesperline;
		size_image = fmt.fmt.pix.sizeimage;
		break;
	default:
		break;
	}

	switch (io) {
	case IO_METHOD_WRITE:
		init_write(fmt.fmt.pix.sizeimage);
		break;

	case IO_METHOD_MMAP:
		init_mmap();
		break;

	case IO_METHOD_USERPTR:
		init_userp(fmt.fmt.pix.sizeimage);
		break;
	}
}

static void close_device(void)
{
	if (-1 == close(fd))
		errno_exit("close");

	fd = -1;
}

static void open_device(void)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name,
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
			strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void usage(FILE *fp, int argc, char **argv)
{
	char fourccstr[5];
	fourccstr[4] = 0;
	fprintf(fp,
		"Usage: %s [options]\n\n"
		"Version 1.3\n"
		"Options:\n"
		"-d | --device name   Video device name [%s]\n"
		"-h | --help          Print this message\n"
		"-m | --mmap          Use memory mapped buffers [default]\n"
		"-w | --write         Use write() calls\n"
		"-u | --userp         Use application allocated buffers\n"
		"-c | --count         Number of frames to create [%i] (negative numbers: no limit)\n"
		"-f | --format        Use format [%dx%d@%s]\n"
		"-t | --timestamp     Set timestamp\n"
		"-r | --fps           Target output FPS [%d]\n"
		"-i | --input         Input file name [%s]\n"
		"",
		argv[0], dev_name, frame_count, width, height,
		fourcc2str(pixelformat, fourccstr), target_fps,
		(input_filename ? input_filename : "none"));
}

static const char short_options[] = "d:hmwuc:f:ti:r:";

static const struct option long_options[] = {
	{ "device", required_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ "mmap", no_argument, NULL, 'm' },
	{ "write", no_argument, NULL, 'w' },
	{ "userp", no_argument, NULL, 'u' },
	{ "count", required_argument, NULL, 'c' },
	{ "format", required_argument, NULL, 'f' },
	{ "timestamp", no_argument, NULL, 't' },
	{ "fps", required_argument, NULL, 'r' },
	{ "input", required_argument, NULL, 'i' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	dev_name = "/dev/video0";

	for (;;) {
		int idx;
		int c;

		c = getopt_long(argc, argv, short_options, long_options, &idx);

		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'h':
			usage(stdout, argc, argv);
			exit(EXIT_SUCCESS);

		case 'm':
			io = IO_METHOD_MMAP;
			break;

		case 'w':
			io = IO_METHOD_WRITE;
			break;

		case 'i':
		  input_filename = optarg;
		  break;

		case 'u':
			io = IO_METHOD_USERPTR;
			break;

		case 'c':
			errno = 0;
			frame_count = strtol(optarg, NULL, 0);
			if (errno)
				errno_exit(optarg);
			break;

		case 'f': {
			int n;
			int w = 0, h = 0;
			char col[5];
			n = sscanf(optarg, "%dx%d@%4c", &w, &h, col);
			if (n == 3) {
				width = (w > 0) ? w : 0;
				height = (h > 0) ? h : 0;
				pixelformat = str2fourcc(col);
				col[4] = 0;
			} else {
				errno_exit(optarg);
			}
			break;
		case 't':
			set_timestamp = 1;
			break;
		case 'r':
			errno = 0;
			target_fps = (int)strtol(optarg, NULL, 10);
			if (errno || target_fps <= 0) {
				fprintf(stderr, "invalid fps: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			if ((unsigned int)target_fps > 1000U) {
				target_fps = 1000;
			}
			frame_interval_us = (unsigned int)(1000000U / (unsigned int)target_fps);
			break;
		}

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	open_device();
	init_device();
	//jonah add open_input_file()
	open_input_file();
	start_capturing();
	mainloop();
	stop_capturing();
	uninit_device();
	close_device();
	fprintf(stderr, "\n");
	return 0;
}
