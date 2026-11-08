/* Minimal media-controller tool (media-ctl/v4l2-ctl are not installed).
 * media: links | enable <id> | topo
 * subdev: get <pad> | fmt <pad> <w> <h> <code>
 * video:  vfmt <w> <h> <code> | stream <count> <outfile>
 */
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
#include <linux/v4l2-subdev.h>

static unsigned fourcc(const char *s)
{
	if (!strcmp(s, "SBGGR10")) return V4L2_PIX_FMT_SBGGR10;
	if (!strcmp(s, "SGBRG10")) return V4L2_PIX_FMT_SGBRG10;
	if (!strcmp(s, "SGRBG10")) return V4L2_PIX_FMT_SGRBG10;
	if (!strcmp(s, "SRGGB10")) return V4L2_PIX_FMT_SRGGB10;
	if (!strcmp(s, "SBGGR8"))  return V4L2_PIX_FMT_SBGGR8;
	if (!strcmp(s, "YUYV"))    return V4L2_PIX_FMT_YUYV;
	if (!strcmp(s, "NV12"))    return V4L2_PIX_FMT_NV12;
	return strtoul(s, NULL, 0);
}

static int cmd_links(const char *dev, int unused)
{
	struct media_entity_desc e;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	(void)unused;
	memset(&e, 0, sizeof(e));
	e.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &e) == 0) {
		struct media_links_enum le;
		struct media_pad_desc pads[8];
		struct media_link_desc links[24];
		unsigned np = e.pads < 8 ? e.pads : 8;
		unsigned nl = e.links < 24 ? e.links : 24;

		if (nl) {
			memset(&le, 0, sizeof(le));
			memset(pads, 0, sizeof(pads));
			memset(links, 0, sizeof(links));
			le.entity = e.id;
			le.pads = pads;
			le.links = links;
			if (ioctl(fd, MEDIA_IOC_ENUM_LINKS, &le) == 0) {
				for (unsigned i = 0; i < nl; i++)
					printf("%s(%u) link %u:%u -> %u:%u %s%s\n",
					       e.name, e.id, links[i].source.entity,
					       links[i].source.index, links[i].sink.entity,
					       links[i].sink.index,
					       (links[i].flags & MEDIA_LNK_FL_ENABLED) ? "ENABLED" : "off",
					       (links[i].flags & MEDIA_LNK_FL_IMMUTABLE) ? " IMMUTABLE" : "");
			}
		}
		e.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}
	close(fd);
	return 0;
}

static int cmd_enable(const char *dev, unsigned src_ent, unsigned src_idx,
		      unsigned snk_ent, unsigned snk_idx, int on)
{
	struct media_link_desc l;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&l, 0, sizeof(l));
	l.source.entity = src_ent;
	l.source.index = src_idx;
	l.sink.entity = snk_ent;
	l.sink.index = snk_idx;
	l.flags = on ? MEDIA_LNK_FL_ENABLED : 0;
	if (ioctl(fd, MEDIA_IOC_SETUP_LINK, &l) < 0) { perror("SETUP_LINK"); close(fd); return 1; }
	printf("link %u:%u -> %u:%u %s\n", src_ent, src_idx, snk_ent, snk_idx, on ? "enabled" : "disabled");
	close(fd); return 0;
}

static int cmd_sub(const char *dev, int set, unsigned pad, int w, int h, unsigned code)
{
	struct v4l2_subdev_format f;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&f, 0, sizeof(f));
	f.pad = pad; f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	if (!set) {
		if (ioctl(fd, VIDIOC_SUBDEV_G_FMT, &f) < 0) { perror("G_FMT"); close(fd); return 1; }
	} else {
		f.format.width = w; f.format.height = h; f.format.code = code;
		f.format.field = V4L2_FIELD_NONE;
		if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) { perror("S_FMT"); close(fd); return 1; }
	}
	printf("%s pad%u: %ux%u code=0x%04x\n", dev, f.pad, f.format.width,
	       f.format.height, f.format.code);
	close(fd); return 0;
}

static int cmd_vfmt(const char *dev, int w, int h, unsigned code)
{
	struct v4l2_format f;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f.fmt.pix.width = w; f.fmt.pix.height = h; f.fmt.pix.pixelformat = code;
	if (ioctl(fd, VIDIOC_S_FMT, &f) < 0) { perror("S_FMT"); close(fd); return 1; }
	printf("%s: %ux%u fourcc=0x%08x sizeimage=%u\n", dev, f.fmt.pix.width,
	       f.fmt.pix.height, f.fmt.pix.pixelformat, f.fmt.pix.sizeimage);
	close(fd); return 0;
}



static int cmd_caps(const char *dev)
{
	struct v4l2_capability c;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	if (ioctl(fd, VIDIOC_QUERYCAP, &c) < 0) { perror("QUERYCAP"); close(fd); return 1; }
	printf("%s: driver=%s card=%s caps=0x%08x devcaps=0x%08x\n", dev, c.driver, c.card,
	       c.capabilities, c.device_caps);
	printf("  VIDEO_CAPTURE=%d MPLANE=%d META=%d\n",
	       !!(c.device_caps & V4L2_CAP_VIDEO_CAPTURE),
	       !!(c.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE),
	       !!(c.device_caps & V4L2_CAP_META_CAPTURE));
	close(fd); return 0;
}

static int cmd_enumfmt(const char *dev)
{
	struct v4l2_fmtdesc d;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&d, 0, sizeof(d));
	d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &d) == 0) {
		printf("[%u] %.4s (%s)\n", d.index, (char *)&d.pixelformat, d.description);
		d.index++;
	}
	close(fd); return 0;
}


static int cmd_menum(const char *dev)
{
	struct v4l2_fmtdesc d;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&d, 0, sizeof(d));
	d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &d) == 0) {
		printf("[%u] %.4s (%s)\n", d.index, (char *)&d.pixelformat, d.description);
		d.index++;
	}
	close(fd); return 0;
}

static int cmd_mfmt(const char *dev, int w, int h, unsigned code)
{
	struct v4l2_format f;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.width = w;
	f.fmt.pix_mp.height = h;
	f.fmt.pix_mp.pixelformat = code;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	f.fmt.pix_mp.num_planes = 1;
	if (ioctl(fd, VIDIOC_S_FMT, &f) < 0) { perror("S_FMT"); close(fd); return 1; }
	printf("%s: %ux%u fourcc=%.4s planes=%u size=%u\n", dev,
	       f.fmt.pix_mp.width, f.fmt.pix_mp.height, (char *)&f.fmt.pix_mp.pixelformat,
	       f.fmt.pix_mp.num_planes, f.fmt.pix_mp.plane_fmt[0].sizeimage);
	close(fd); return 0;
}

static int cmd_mstream(const char *dev, int n, const char *out)
{
	int fd = open(dev, O_RDWR);
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer b;
	struct v4l2_plane planes[1];
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	void *m[4]; size_t len[4];
	if (fd < 0) { perror(dev); return 1; }
	memset(&rb, 0, sizeof(rb));
	rb.count = 4; rb.type = type; rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); close(fd); return 1; }
	if (rb.count < 2) { fprintf(stderr, "only %u buffers\n", rb.count); }
	for (unsigned i = 0; i < rb.count; i++) {
		memset(&b, 0, sizeof(b));
		memset(planes, 0, sizeof(planes));
		b.type = type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
		b.m.planes = planes; b.length = 1;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 1; }
		len[i] = planes[0].length;
		m[i] = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			    planes[0].m.mem_offset);
		if (m[i] == MAP_FAILED) { perror("mmap"); return 1; }
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 1; }
	}
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); close(fd); return 1; }
	printf("streaming, buffers=%u size=%zu\n", rb.count, len[0]);
	FILE *fp = out ? fopen(out, "wb") : NULL;
	for (int i = 0; i < n; i++) {
		memset(&b, 0, sizeof(b));
		memset(planes, 0, sizeof(planes));
		b.type = type; b.memory = V4L2_MEMORY_MMAP;
		b.m.planes = planes; b.length = 1;
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); break; }
		printf("frame %d bytes=%u seq=%u\n", i, planes[0].bytesused, b.sequence);
		if (fp) { fwrite(m[b.index], 1, planes[0].bytesused, fp); fflush(fp); }
		ioctl(fd, VIDIOC_QBUF, &b);
	}
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (fp) fclose(fp);
	close(fd); return 0;
}

static int cmd_stream(const char *dev, int n, const char *out)
{
	int fd = open(dev, O_RDWR);
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer b;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	void *m[4]; size_t len[4];
	if (fd < 0) { perror(dev); return 1; }
	memset(&rb, 0, sizeof(rb));
	rb.count = 4; rb.type = type; rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); close(fd); return 1; }
	for (unsigned i = 0; i < rb.count; i++) {
		memset(&b, 0, sizeof(b));
		b.type = type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 1; }
		len[i] = b.length;
		m[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
		if (m[i] == MAP_FAILED) { perror("mmap"); return 1; }
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 1; }
	}
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); close(fd); return 1; }
	FILE *fp = out ? fopen(out, "wb") : NULL;
	for (int i = 0; i < n; i++) {
		memset(&b, 0, sizeof(b));
		b.type = type; b.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); break; }
		printf("frame %d bytes=%u seq=%u\n", i, b.bytesused, b.sequence);
		if (fp) { fwrite(m[b.index], 1, b.bytesused, fp); fflush(fp); }
		ioctl(fd, VIDIOC_QBUF, &b);
	}
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (fp) fclose(fp);
	close(fd); return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage:\n  %s <media> links\n  %s <media> enable <src_ent> <src_pad> <snk_ent> <snk_pad> [0|1]\n"
		        "  %s <subdev> get <pad>\n  %s <subdev> fmt <pad> <w> <h> <fourcc>\n"
		        "  %s <video> vfmt <w> <h> <fourcc>\n  %s <video> stream <n> [out]\n",
		        argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}
	if (!strcmp(argv[2], "links")) return cmd_links(argv[1], 0);
	if (!strcmp(argv[2], "enable") && argc >= 8)
		return cmd_enable(argv[1], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
	if (!strcmp(argv[2], "get") && argc >= 4) return cmd_sub(argv[1], 0, atoi(argv[3]), 0, 0, 0);
	if (!strcmp(argv[2], "fmt") && argc >= 7)
		return cmd_sub(argv[1], 1, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), fourcc(argv[6]));
	if (!strcmp(argv[2], "caps")) return cmd_caps(argv[1]);
	if (!strcmp(argv[2], "enumfmt")) return cmd_enumfmt(argv[1]);
	if (!strcmp(argv[2], "menum")) return cmd_menum(argv[1]);
	if (!strcmp(argv[2], "mfmt") && argc >= 6)
		return cmd_mfmt(argv[1], atoi(argv[3]), atoi(argv[4]), fourcc(argv[5]));
	if (!strcmp(argv[2], "mstream") && argc >= 4)
		return cmd_mstream(argv[1], atoi(argv[3]), argc > 4 ? argv[4] : NULL);
	if (!strcmp(argv[2], "vfmt") && argc >= 6)
		return cmd_vfmt(argv[1], atoi(argv[3]), atoi(argv[4]), fourcc(argv[5]));
	if (!strcmp(argv[2], "stream") && argc >= 4)
		return cmd_stream(argv[1], atoi(argv[3]), argc > 4 ? argv[4] : NULL);
	fprintf(stderr, "bad usage\n"); return 2;
}
