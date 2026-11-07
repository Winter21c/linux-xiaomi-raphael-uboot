// Minimal NFC netlink test: bring the NCI device up and start a poll.
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nfc.h>

#ifndef NLA_ALIGNTO
#define NLA_ALIGNTO 4
#define NLA_ALIGN(len) (((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN ((int)NLA_ALIGN(sizeof(struct nlattr)))
#endif
#ifndef NLA_DATA
#define NLA_DATA(nla) ((void *)((char *)(nla) + NLA_HDRLEN))
#endif
#ifndef NLA_OK
#define NLA_OK(nla, len) ((len) >= (int)sizeof(struct nlattr) && \
			  (nla)->nla_len >= sizeof(struct nlattr) && \
			  (nla)->nla_len <= (len))
#define NLA_NEXT(nla, attrlen) ((attrlen) -= NLA_ALIGN((nla)->nla_len), \
				(struct nlattr *)(((char *)(nla)) + NLA_ALIGN((nla)->nla_len)))
#endif

static int send_msg(int fd, void *buf, size_t len)
{
	struct sockaddr_nl nl = { .nl_family = AF_NETLINK };
	return sendto(fd, buf, len, 0, (struct sockaddr *)&nl, sizeof(nl));
}

static int recv_ack(int fd)
{
	char buf[8192];
	int n = recv(fd, buf, sizeof(buf), 0);
	if (n < 0) return -errno;
	struct nlmsghdr *nlh = (void *)buf;
	if (nlh->nlmsg_type == NLMSG_ERROR) {
		struct nlmsgerr *e = NLMSG_DATA(nlh);
		return e->error;
	}
	return 0;
}

static int get_family(int fd, const char *name)
{
	char buf[1024] = {0};
	struct nlmsghdr *nlh = (void *)buf;
	struct genlmsghdr *gh;
	struct nlattr *na;

	nlh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	nlh->nlmsg_type = GENL_ID_CTRL;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	nlh->nlmsg_seq = 1;
	gh = NLMSG_DATA(nlh);
	gh->cmd = CTRL_CMD_GETFAMILY;
	gh->version = 1;
	na = (void *)((char *)gh + GENL_HDRLEN);
	na->nla_type = CTRL_ATTR_FAMILY_NAME;
	na->nla_len = NLA_HDRLEN + strlen(name) + 1;
	strcpy((char *)NLA_DATA(na), name);
	nlh->nlmsg_len += NLA_ALIGN(na->nla_len);

	if (send_msg(fd, buf, nlh->nlmsg_len) < 0) return -errno;

	int n = recv(fd, buf, sizeof(buf), 0);
	if (n < 0) return -errno;
	nlh = (void *)buf;
	gh = NLMSG_DATA(nlh);
	int len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	for (na = (void *)((char *)gh + GENL_HDRLEN); NLA_OK(na, len); na = NLA_NEXT(na, len))
		if (na->nla_type == CTRL_ATTR_FAMILY_ID)
			return *(unsigned short *)NLA_DATA(na);
	return -ENOENT;
}

static int nfc_cmd(int fd, int fam, int cmd, int attr, int value)
{
	char buf[512] = {0};
	struct nlmsghdr *nlh = (void *)buf;
	struct genlmsghdr *gh;
	struct nlattr *na;

	nlh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	nlh->nlmsg_type = fam;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = 2;
	gh = NLMSG_DATA(nlh);
	gh->cmd = cmd;
	gh->version = NFC_GENL_VERSION;
	na = (void *)((char *)gh + GENL_HDRLEN);
	na->nla_type = attr;
	na->nla_len = NLA_HDRLEN + sizeof(int);
	*(int *)NLA_DATA(na) = value;
	nlh->nlmsg_len += NLA_ALIGN(na->nla_len);

	if (send_msg(fd, buf, nlh->nlmsg_len) < 0) return -errno;
	return recv_ack(fd);
}


static int nfc_start_poll(int fd, int fam, int dev)
{
	char buf[512] = {0};
	struct nlmsghdr *nlh = (void *)buf;
	struct genlmsghdr *gh;
	struct nlattr *na;
	int protos = NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |
		     NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO14443_MASK |
		     NFC_PROTO_NFC_DEP_MASK | NFC_PROTO_ISO15693_MASK;

	nlh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	nlh->nlmsg_type = fam;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = 3;
	gh = NLMSG_DATA(nlh);
	gh->cmd = NFC_CMD_START_POLL;
	gh->version = NFC_GENL_VERSION;

	na = (void *)((char *)gh + GENL_HDRLEN);
	na->nla_type = NFC_ATTR_DEVICE_INDEX;
	na->nla_len = NLA_HDRLEN + sizeof(int);
	*(int *)NLA_DATA(na) = dev;
	nlh->nlmsg_len += NLA_ALIGN(na->nla_len);

	na = (void *)((char *)nlh + nlh->nlmsg_len);
	na->nla_type = NFC_ATTR_PROTOCOLS;
	na->nla_len = NLA_HDRLEN + sizeof(int);
	*(int *)NLA_DATA(na) = protos;
	nlh->nlmsg_len += NLA_ALIGN(na->nla_len);

	na = (void *)((char *)nlh + nlh->nlmsg_len);
	na->nla_type = NFC_ATTR_RF_MODE;
	na->nla_len = NLA_HDRLEN + 1;
	*(char *)NLA_DATA(na) = 0; /* NFC_RF_INITIATOR */
	nlh->nlmsg_len += NLA_ALIGN(na->nla_len);

	if (send_msg(fd, buf, nlh->nlmsg_len) < 0) return -errno;
	return recv_ack(fd);
}

int main(void)
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	struct sockaddr_nl nl = { .nl_family = AF_NETLINK };
	int dev = 0;

	if (fd < 0) { perror("socket"); return 1; }
	if (bind(fd, (void *)&nl, sizeof(nl)) < 0) { perror("bind"); return 1; }

	int fam = get_family(fd, "nfc");
	if (fam < 0) { printf("no nfc family: %d\n", fam); return 1; }
	printf("nfc genl family id %d\n", fam);

	int ret = nfc_cmd(fd, fam, NFC_CMD_DEV_UP, NFC_ATTR_DEVICE_INDEX, dev);
	printf("DEV_UP: %s (%d)\n", ret ? strerror(-ret) : "ok", ret);

	ret = nfc_start_poll(fd, fam, dev);
	printf("START_POLL: %s (%d)\n", ret ? strerror(-ret) : "ok", ret);
	return 0;
}
