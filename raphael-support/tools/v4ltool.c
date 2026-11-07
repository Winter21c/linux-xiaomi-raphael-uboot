/* Minimal media/V4L2 tool: the image has neither media-ctl nor v4l2-ctl. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/media.h>
#include <linux/videodev2.h>

static int topo(const char *dev)
{
	struct media_device_info info;
	struct media_entity_desc ent;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	if (ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info) == 0)
		printf("driver=%s model=%s\n", info.driver, info.model);
	memset(&ent, 0, sizeof(ent));
	ent.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &ent) == 0) {
		printf("entity %u: %s", ent.id, ent.name);
		if (ent.type == MEDIA_ENT_T_DEVNODE_V4L)
			printf(" (video node)\n");
		else
			printf(" (pads=%u links=%u)\n", ent.pads, ent.links);
		ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}
	close(fd);
	return 0;
}

static int setfmt(const char *dev, int w, int h, unsigned code)
{
	int fd = open(dev, O_RDWR);
	struct v4l2_format f;
	if (fd < 0) { perror(dev); return 1; }
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f.fmt.pix.width = w; f.fmt.pix.height = h; f.fmt.pix.pixelformat = code;
	if (ioctl(fd, VIDIOC_S_FMT, &f) < 0) { perror("S_FMT"); close(fd); return 1; }
	printf("%s: %ux%u fourcc=%08x sizeimage=%u\n", dev, f.fmt.pix.width,
	       f.fmt.pix.height, f.fmt.pix.pixelformat, f.fmt.pix.sizeimage);
	close(fd); return 0;
}

static int stream(const char *dev, int nframes, const char *out)
{
	int fd = open(dev, O_RDWR);
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	void *mem[4]; size_t len[4];
	if (fd < 0) { perror(dev); return 1; }
	memset(&rb, 0, sizeof(rb));
	rb.count = 4; rb.type = type; rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); return 1; }
	for (unsigned i = 0; i < rb.count; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = type; buf.memory = V4L2_MEMORY_MMAP; buf.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("QUERYBUF"); return 1; }
		len[i] = buf.length;
		mem[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		ioctl(fd, VIDIOC_QBUF, &buf);
	}
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); return 1; }
	FILE *fp = out ? fopen(out, "wb") : NULL;
	for (int i = 0; i < nframes; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = type; buf.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) { perror("DQBUF"); break; }
		printf("frame %d bytes=%u seq=%u\n", i, buf.bytesused, buf.sequence);
		if (fp) { fwrite(mem[buf.index], 1, buf.bytesused, fp); fflush(fp); }
		ioctl(fd, VIDIOC_QBUF, &buf);
	}
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (fp) fclose(fp);
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s topo /dev/media0 | fmt /dev/videoN w h fourcc | stream /dev/videoN n out\n", argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "topo")) return topo(argc > 2 ? argv[2] : "/dev/media0");
	if (!strcmp(argv[1], "fmt") && argc >= 6)
		return setfmt(argv[2], atoi(argv[3]), atoi(argv[4]), strtoul(argv[5], NULL, 0));
	if (!strcmp(argv[1], "stream") && argc >= 4)
		return stream(argv[2], atoi(argv[3]), argc > 4 ? argv[4] : NULL);
	fprintf(stderr, "bad usage\n");
	return 2;
}
