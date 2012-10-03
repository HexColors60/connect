
#include "connect.h"

#define cnct_mtu 64*1024

int cnct_packet_print(char *packet, int len)
{
	/* TODO: FIXME: if proto == IPPROTO_IP && cnct_sys == LINUX { seek packet to IP header before process } */
	int i;
	for (i = 0; i < len; i++) {
		printf("%02X", *((unsigned char *) packet + i));
		if (i == 14) {
			printf("\n");
			break;
		} else {
			printf(" ");
		}
	}
	return 0;
}

int cnct_packet_promisc()
{
	return 0;
}

socket_t cnct_packet_socket(int engine, int proto)
{
	LOG_IN;
	
	int rs;
#ifdef CNCT_SYS_LINUX
	if (engine == CNCT_PACKENGINE_BPF) {
		rs = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	} else
#endif
		(proto == IPPROTO_RAW) ? (rs = socket(CNCT_SOCKET_RAW)) : (rs = socket(CNCT_SOCKET_IP));
	
	if (rs == CNCT_INVALID) {
		perror("socket");
		LOG_OUT_RET(-1);
	}
	
	LOG_OUT;
	
	return rs;
}

int cnct_packet_recv(socket_t rs)
{
	LOG_IN;
	
	int rx = 0;
	
	MALLOC_TYPE_SIZE(char, packet, cnct_mtu);
	
	while (1) {
		memset(packet, '\0', cnct_mtu);
		rx = recvfrom(rs, packet, cnct_mtu, 0, NULL, NULL);
		cnct_packet_print(packet, rx);
		//break;
	}
	
	LOG_OUT;
	
	return 0;
}
/*
int cnct_filter_bpf(socket_t sd)
{
#ifdef CNCT_UNIXWARE
	struct sock_filter bpf[] = {
		{ 0x6, 0, 0, 0x0000ffff }
	};
	struct sock_fprog fprog;
	
	fprog.len = 1;
	fprog.filter = bpf;
	
	if (setsockopt(sd, SOL_SOCKET, SO_ATTACH_FILTER, &fprog, sizeof(fprog)) < 0) {
		perror("setsockopt");
		cnct_socket_close(sd);
		return -1;
	}
#endif
	return 0;
}
*/
int cnct_filter_pcp(char *rule)
{
	return 0;
}

int cnct_packet_dump(int type, char *iface, char *rule) /* engine, interface, proto, rule */
{
	LOG_IN;
	int proto = IPPROTO_IP;
	/*
	 * doing the following things here:
	 * - rule exists? using PCAP
	 * - no rule?
	 *  -- using TYPE, but:
	 *  --- BPF on NT? NO. _USR only
	 *  --- USR on BSD/OSX? NO. _BPF only
	 */
	
	/*
		linux   : usr  bpf  pcp
		bsd/osx : usr* bpf  pcp
		win     : usr  usr* pcp
		_____
		* not all packets
	*/
	
	/* TODO: develop default policies (if type/iface/proto/rule/... not provided) */
	
	socket_t rs;
	
	if (rule) {
		type = CNCT_PACKENGINE_PCP;
	}
	
	if (!type) {
		/* trying to set up default type if not provided */
		cnct_sys == CNCT_SYS_NT_T ? (type = CNCT_PACKENGINE_USR) : (type = CNCT_PACKENGINE_BPF);
	}
	
	if ((cnct_api == CNCT_API_NT_TYPE) && (type == CNCT_PACKENGINE_BPF)) {
		type = CNCT_PACKENGINE_USR;
	}
	
	//if (((cnct_sys == CNCT_SYS_BSD_T) || (cnct_sys == CNCT_SYS_OSX_T)) && (type == CNCT_PACKENGINE_USR)) {
	//if (((cnct_sys == CNCT_SYS_OSX_T)) && (type == CNCT_PACKENGINE_USR)) {
	/*
	if (type == CNCT_PACKENGINE_USR) {
		type = CNCT_PACKENGINE_BPF;
	}
	*/
	
	DBG_ON(printf("type: %d\n", type);)
	
	if (type != CNCT_PACKENGINE_PCP) {
		if ((rs = cnct_packet_socket(type, proto)) == CNCT_INVALID) {
			printf("error: can't set socket for dump\n");
			return 1;
		}
	}
	
	if (type == CNCT_PACKENGINE_BPF) {
		if (cnct_filter_bpf(iface, rs) == CNCT_ERROR) {
			printf("error: can't set BPF filter\n");
			return CNCT_ERROR;
		}
	} else if (type == CNCT_PACKENGINE_PCP) {
		cnct_filter_pcp(rule);
	} else if (type == CNCT_PACKENGINE_USR) {
	#ifdef CNCT_SYS_NT
		//if (proto == IP) {
			cnct_filter_bpf(); /* proto */
		//}
	#else
		cnct_packet_recv(rs);
	#endif /* CNCT_SYS_NT */
		;
	} else {
		printf("type not supported\n");
		return 1;
	}
	
	//cnct_packet_recv(rs);
	
	LOG_OUT;
	
	return 0;
}

