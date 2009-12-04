/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/* LINTLIBRARY */
/* PROTOLIB1 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * nfsstat: Network File System statistics
 *
 */
#include "nfsstat_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <kvm.h>
#include <kstat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/tiuser.h>
#include <sys/statvfs.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/mkdev.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc.h>
#include <netdir.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/nfs_sec.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <signal.h>
#include <time.h>
#include <strings.h>
#include <ctype.h>
#include <locale.h>
#include <assert.h>

#include "statcommon.h"
#include <nfs/nfssys.h>
extern int _nfssys(int, void *);

static kstat_ctl_t *kc = NULL;		/* libkstat cookie */
static kstat_t *rpc_clts_client_kstat, *rpc_clts_server_kstat;
static kstat_t *rpc_cots_client_kstat, *rpc_cots_server_kstat;
static kstat_t *rpc_rdma_client_kstat, *rpc_rdma_server_kstat;
static kstat_t *nfs_client_kstat, *nfs_server_v2_kstat, *nfs_server_v3_kstat;
static kstat_t *nfs4_client_kstat, *nfs_server_v4_kstat;
static kstat_t *rfsproccnt_v2_kstat, *rfsproccnt_v3_kstat, *rfsproccnt_v4_kstat;
static kstat_t *rfsreqcnt_v2_kstat, *rfsreqcnt_v3_kstat, *rfsreqcnt_v4_kstat;
static kstat_t *nfs41_client_kstat, *rfsproccnt_v41_kstat, *rfsreqcnt_v41_kstat;
static kstat_t *aclproccnt_v2_kstat, *aclproccnt_v3_kstat;
static kstat_t *aclreqcnt_v2_kstat, *aclreqcnt_v3_kstat;
static kstat_t *nfs4_debug_kstat, *nfs_debug_kstat;
static kstat_t *ksum_kstat;

static void handle_sig(int);
static int getstats_rpc(void);
static int getstats_nfs(void);
static int getstats_rfsproc(int);
static int getstats_rfsreq(int);
static int getstats_aclproc(void);
static int getstats_aclreq(void);
static void print_layoutstats(char *, struct  pnfs_getflo_args *);
static int getflostats_pnfs(char *);
static void putstats(void);
static void setup(void);
static void cr_print(int);
static void sr_print(int);
static void cn_print(int, int);
static void sn_print(int, int);
static void ca_print(int, int);
static void sa_print(int, int);
static void req_print(kstat_t *, kstat_t *, int, int, int);
static void req_print_v4(kstat_t *, kstat_t *, int, int, int);
static void stat_print(const char *, kstat_t *, kstat_t *, int, int);
static void nfsstat_kstat_sum(kstat_t *, kstat_t *, kstat_t *, kstat_t *);
static void stats_timer(int);
static void safe_zalloc(void **, uint_t, int);
static int safe_strtoi(char const *, char *);


static void nfsstat_kstat_copy(kstat_t *, kstat_t *, int);
static kid_t safe_kstat_read(kstat_ctl_t *, kstat_t *, void *);
static kid_t safe_kstat_write(kstat_ctl_t *, kstat_t *, void *);

static void usage(void);
static void mi_print(void);
static int ignore(char *);
static int interval;		/* interval between stats */
static int count;		/* number of iterations the stat is printed */
/*
 * NFS ops that are not supported in NFS version 4, minor version 1, must
 * be included here as follows.
 */
#define	NUM_OPS_TOSKIP 5
nfs_opnum4 ops_toskip_v41[NUM_OPS_TOSKIP] =  {
	OP_OPEN_CONFIRM,
	OP_RENEW,
	OP_SETCLIENTID,
	OP_SETCLIENTID_CONFIRM,
	OP_RELEASE_LOCKOWNER
};

#define	MAX_COLUMNS	80
#define	ALL_ONES	0xffffffffffffffffull
#define	MAX_PATHS	50	/* max paths that can be taken by -m */
/*
 * MI4_MIRRORMOUNT is canonically defined in nfs4_clnt.h, but we cannot
 * include that file here.
 */
#define	MI4_MIRRORMOUNT	0x4000
#define	NFS_V4		4

static int req_width(kstat_t *, int);
static int stat_width(kstat_t *, int);
static char *path [MAX_PATHS] = {NULL};  /* array to store the multiple paths */

/*
 * Struct holds the previous kstat values so
 * we can compute deltas when using the -i flag
 */
typedef struct old_kstat
{
	kstat_t kst;
	int tot;
} old_kstat_t;

static old_kstat_t old_rpc_clts_client_kstat, old_rpc_clts_server_kstat;
static old_kstat_t old_rpc_cots_client_kstat, old_rpc_cots_server_kstat;
static old_kstat_t old_rpc_rdma_client_kstat, old_rpc_rdma_server_kstat;
static old_kstat_t old_nfs_client_kstat, old_nfs_server_v2_kstat;
static old_kstat_t old_nfs_server_v3_kstat, old_ksum_kstat;
static old_kstat_t old_nfs4_client_kstat, old_nfs41_client_kstat;
static old_kstat_t old_nfs4_debug_kstat, old_nfs_debug_kstat;
static old_kstat_t old_nfs_server_v4_kstat, old_rfsproccnt_v2_kstat;
static old_kstat_t old_rfsproccnt_v3_kstat, old_rfsproccnt_v4_kstat;
static old_kstat_t old_rfsreqcnt_v2_kstat;
static old_kstat_t old_rfsreqcnt_v3_kstat, old_rfsreqcnt_v4_kstat;
static old_kstat_t old_rfsreqcnt_v41_kstat;
static old_kstat_t old_aclproccnt_v2_kstat, old_aclproccnt_v3_kstat;
static old_kstat_t old_aclreqcnt_v2_kstat, old_aclreqcnt_v3_kstat;

static uint_t timestamp_fmt = NODATE;

#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"		/* Use this only if it isn't */
#endif

int
main(int argc, char *argv[])
{
	int c, go_forever, j;
	int cflag = 0;		/* client stats */
	int sflag = 0;		/* server stats */
	int nflag = 0;		/* nfs stats */
	int rflag = 0;		/* rpc stats */
	int mflag = 0;		/* mount table stats */
	int aflag = 0;		/* print acl statistics */
	int vflag = 0;		/* version specified, 0 specifies all */
	int zflag = 0;		/* zero stats after printing */
	int lflag = 0;		/* layout stats */
	char *fname;		/* filename for the layout */
	char *split_line = "*******************************************"
	    "*************************************";

	interval = 0;
	count = 0;
	go_forever = 0;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "cnrsmzav:T:l:")) != EOF) {
		switch (c) {
		case 'l':
			lflag++;
			fname = optarg;
			break;
		case 'c':
			cflag++;
			break;
		case 'n':
			nflag++;
			break;
		case 'r':
			rflag++;
			break;
		case 's':
			sflag++;
			break;
		case 'm':
			mflag++;
			break;
		case 'z':
			if (geteuid())
				fail(0, "Must be root for z flag\n");
			zflag++;
			break;
		case 'a':
			aflag++;
			break;
		case 'v':
			vflag = atoi(optarg);
			if (vflag != 41) {
				if ((vflag < 2) || (vflag > 4)) {
					fail(0, "Invalid version number\n");
				}
			}
			break;
		case 'T':
			if (optarg) {
				if (*optarg == 'u')
					timestamp_fmt = UDATE;
				else if (*optarg == 'd')
					timestamp_fmt = DDATE;
				else
					usage();
			} else {
				usage();
			}
			break;
		case '?':
		default:
			usage();
		}
	}
	if (lflag) {
		return (getflostats_pnfs(fname));
	}

	if (((argc - optind) > 0) && !mflag) {

		interval = safe_strtoi(argv[optind], "invalid interval");
		if (interval < 1)
			fail(0, "invalid interval\n");
		optind++;

		if ((argc - optind) > 0) {
			count = safe_strtoi(argv[optind], "invalid count");
			if ((count <= 0) || (count == NULL))
				fail(0, "invalid count\n");
		}
		optind++;

		if ((argc - optind) > 0)
			usage();

		/*
		 * no count number was set, so we will loop infinitely
		 * at interval specified
		 */
		if (!count)
			go_forever = 1;
		stats_timer(interval);
	} else if (mflag) {
		if (lflag || cflag || rflag || sflag || zflag || nflag ||
		    aflag || vflag) {
			fail(0, "The -m flag may not be used"
			    " with any other flags");
		}

		for (j = 0; (argc - optind > 0) && (j < (MAX_PATHS - 1)); j++) {
			path[j] =  argv[optind];
			if (*path[j] != '/')
				fail(0, "Please fully qualify your pathname "
				    "with a leading '/'");
			optind++;
		}
		path[j] = NULL;
		if (argc - optind > 0)
			fprintf(stderr, "Only the first 50 paths "
			    "will be searched for\n");
	}

	setup();

	do {
		if (mflag) {
			mi_print();
		} else {
			if (timestamp_fmt != NODATE)
				print_timestamp(timestamp_fmt);

			if (sflag &&
			    (rpc_clts_server_kstat == NULL ||
			    nfs_server_v4_kstat == NULL)) {
				fprintf(stderr,
				    "nfsstat: kernel is not configured with "
				    "the server nfs and rpc code.\n");
			}

			/* if s and nothing else, all 3 prints are called */
			if (sflag || (!sflag && !cflag)) {
				if (rflag || (!rflag && !nflag && !aflag))
					sr_print(zflag);
				if (nflag || (!rflag && !nflag && !aflag))
					sn_print(zflag, vflag);
				if (aflag || (!rflag && !nflag && !aflag))
					sa_print(zflag, vflag);
			}
			if (cflag &&
			    (rpc_clts_client_kstat == NULL ||
			    nfs_client_kstat == NULL)) {
				fprintf(stderr,
				    "nfsstat: kernel is not configured with"
				    " the client nfs and rpc code.\n");
			}
			if (cflag || (!sflag && !cflag)) {
				if (rflag || (!rflag && !nflag && !aflag))
					cr_print(zflag);
				if (nflag || (!rflag && !nflag && !aflag))
					cn_print(zflag, vflag);
				if (aflag || (!rflag && !nflag && !aflag))
					ca_print(zflag, vflag);
			}
		}

		if (zflag)
			putstats();
		if (interval)
			printf("%s\n", split_line);

		if (interval > 0)
			(void) pause();
	} while ((--count > 0) || go_forever);

	kstat_close(kc);
	free(ksum_kstat);
	return (0);
}



static int getflostats_pnfs(char *fname) {

	int buf_length = DEFAULT_LAYOUT_SIZE;
	struct  pnfs_getflo_args plo_args;
	layoutstats_t *stats;

	if (fname == NULL) {
		fail(0, "Filename must be provided");
	}
	plo_args.fname = fname;
	plo_args.kernel_bufsize = malloc(sizeof (uint32_t));
	if (plo_args.kernel_bufsize == NULL) {
		fail(0, "Cannot allocate memory");
	}

retry:
	plo_args.layoutstats = malloc(buf_length);
	if (plo_args.layoutstats == NULL) {
		fail(0, "Cannot allocate memory");
	}
	plo_args.user_bufsize = buf_length;

	/*
	 * Make a system call to collect layout statistics
	 */
	_nfssys(NFSSTAT_LAYOUT, &plo_args);

	switch (errno) {

	case 0:	/* SUCCESS! */
		print_layoutstats(fname, &plo_args);
		break;

	case EOVERFLOW: /* Increase size of buffer and retry */
		free(plo_args.layoutstats);
		buf_length = *(plo_args.kernel_bufsize);
		/* Reset errno or else we will be looping forever. */
		errno = 0;
		goto retry;

	case ENOLAYOUT:
		fprintf(stderr, "Layout unacquired\n");
		break;

	case ENOTAFILE:
		fprintf(stderr, "Not a regular file\n");
		break;

	case ENOPNFSSERV:
		fprintf(stderr, "Not a pNFS file\n");
		break;

	case ESYSCALL:
		fprintf(stderr, "System call error\n");
		break;

	case ENONFS:
		fprintf(stderr, "Not an NFS v4 file\n");
		break;

	default: /* Other things went wrong */
		if (errno > 0) {
			fprintf(stderr, strerror(errno));
			fprintf(stderr, "\n");
		} else {
			fprintf(stderr, "Unknown error");
		}

	}

	free(plo_args.layoutstats);
	free(plo_args.kernel_bufsize);

	return (errno);
}

/*
 * Appends microseconds to the string returned by the ctime function.
 */
static
void append_musec(int64_t timestamp_sec, int64_t timestamp_musec,
    char *str_time)
{
	int unit, i, new_str_count;
	int str_len;
	char record[MAX_COLUMNS];
	char str_musec[MAX_COLUMNS], *str_musec_begin, *str_musec_end;
	/*
	 * ctime library function takes pointer to a long.
	 */
	long timestamp_sec_long = timestamp_sec;
	unit = 0;
	new_str_count = 0;
	sprintf(record, ctime(&timestamp_sec_long));
	str_len = strlen(record);
	for (i = 0; i <= str_len; i++) {
		str_time[new_str_count] = record[i];
		new_str_count++;
		if (record[i] == ':') {
			unit++;
			continue;
		}
		if (unit == 2 && record[i + 1] == ' ') {
			str_time[new_str_count] = ':';
			str_musec_begin = lltostr(
			    timestamp_musec, &str_musec[MAX_COLUMNS - 1]);
			str_musec[MAX_COLUMNS - 1] = '\0';
			strcpy(str_time + new_str_count + 1, str_musec_begin);
			new_str_count = strlen(str_time);
			unit++;
		}
	}
}

static void
print_layoutstats(char *filename, struct  pnfs_getflo_args *plo_args)
{
	layoutstats_t *stats;
	layoutstats_t *decoded_stats;
	layoutspecs_t	*los;
	stripe_info_t *si_list_val;
	netaddr4 *mpl_list_val;
	layoutiomode4 iomode;
	XDR xdr;
	netaddr4 *na;
	enum clnt_stat ds_status;
	long port;
	uint32_t kernel_bufsize, si_list_len;
	uint32_t lo_status;
	int cur_rec_size, j, error;
	int mpl_len;
	char record[4096], str_time[MAX_COLUMNS], *buf_pos;
	char hostname[2048], ipaddress[1024], netid[1024];
	int lonum, i = 0;

	/*
	 * XDR decode the buffer that came from the kernel
	 */
	kernel_bufsize = *(plo_args->kernel_bufsize);
	stats = (layoutstats_t *)(plo_args->layoutstats);
	if (stats == NULL) {
		fprintf(stderr, "Unable to gather statistics"
		    " from the kernel\n");
		return;
	}
	xdrmem_create(&xdr, (char *)stats, kernel_bufsize, XDR_DECODE);
	decoded_stats = malloc(kernel_bufsize);
	if (decoded_stats == NULL) {
		fprintf(stderr, "Unable to allocate memory\n");
		return;
	}
	if (! xdr_layoutstats_t(&xdr, decoded_stats)) {
		free(decoded_stats);
		fprintf(stderr, "Failure in decoding kernel data structures\n");
		return;
	}


	/*
	 * Print out the details embedded in the decoded structure.
	 */
	sprintf(record, "Number of layouts: %d",
	    decoded_stats->plo_data.total_layouts);
	printf("%-s\n", record);
	sprintf(record, "Proxy I/O count: %" PRIu64,
	    decoded_stats->proxy_iocount);
	printf("%-s\n", record);
	sprintf(record, "DS I/O count: %" PRIu64,
	    decoded_stats->ds_iocount);
	printf("%-s\n", record);
	if (decoded_stats->plo_data.total_layouts == 0) {
		free(decoded_stats);
		return;
	}

	for (lonum = 0; lonum < decoded_stats->plo_data.total_layouts;
	    lonum++) {
		sprintf(record, "Layout [%d]:", lonum);
		printf("%s\n", record);

		los = &decoded_stats->plo_data.lo_specs[lonum];

		if (los->plo_creation_sec != 0 &&
		    los->plo_creation_musec != 0) {
			append_musec(los->plo_creation_sec,
			    los->plo_creation_musec, str_time);
			printf("\tLayout obtained at: %s", str_time);
		}
		iomode = los->iomode;
		lo_status = los->plo_status;
		if (lo_status & (PLO_UNAVAIL | PLO_GET | PLO_BAD |
		    PLO_RETURN | PLO_RECALL)) {
			sprintf(record, "status: UNAVAILABLE");
		} else {
			sprintf(record, "status: AVAILABLE");
		}
		if (lo_status & PLO_TRYLATER) {
			sprintf(record, "status: TRYLATER");
		}

		cur_rec_size = strlen(record);
		buf_pos = record;
		record[cur_rec_size] = ',';
		buf_pos += cur_rec_size + 1;
		switch (iomode) {
		case LAYOUTIOMODE4_READ:
			sprintf(buf_pos, " iomode: LAYOUTIOMODE_READ");
			break;
		case LAYOUTIOMODE4_RW:
			sprintf(buf_pos, " iomode: LAYOUTIOMODE_RW");
			break;
		case LAYOUTIOMODE4_ANY:
			sprintf(buf_pos, " iomode: LAYOUTIOMODE_ANY");
			break;
		default:
			sprintf(buf_pos, "iomode INVALID");
			break;
		}

		printf("\t%-s\n", record);
		if (los->plo_length == ALL_ONES) {
			sprintf(record, "offset: %" PRIu64 ", length: EOF",
			    los->plo_offset);
		} else {
			sprintf(record, "offset: %" PRIu64 ", length: %"
			    PRIu64, los->plo_offset, los->plo_length);
		}
		printf("\t%-s\n", record);
		sprintf(record, "num stripes: %d, stripe unit: %d",
		    los->plo_stripe_count, los->plo_stripe_unit);
		printf("\t%-s\n", record);

		/*
		 * Print data server specific information for each stripe of
		 * the layout.
		 */
		si_list_val = los->plo_stripe_info_list.
		    plo_stripe_info_list_val;
		if (los->plo_stripe_count == 0 || si_list_val == NULL) {
			printf("los stcnt %d, list cnt %d, listval %p",
			    los->plo_stripe_count,
			    los->plo_stripe_info_list.plo_stripe_info_list_len,
			    los->plo_stripe_info_list.
			    plo_stripe_info_list_val);
			sprintf(record, "Data server information not"
			    " available");
			printf("\t%s\n", record);
			continue;
		}
		do {
			sprintf(record, "\tStripe [%d]:", i);
			printf("%-s\n", record);
			mpl_len = si_list_val[i].multipath_list.
			    multipath_list_len;
			mpl_list_val = si_list_val[i].
			    multipath_list.multipath_list_val;
			if (mpl_len != 0 && mpl_list_val != NULL) {
				for (j = 0; j < mpl_len; j++) {
					na = &(mpl_list_val[j]);
					error = lookup_name_port(na, &port,
					    hostname, ipaddress);
					if (error == 0) {
						sprintf(record, "%s:%s:%s:%ld",
						    na->na_r_netid,
						    hostname, ipaddress,
						    port);
					}
					if (error == EADDRDEC) {
						sprintf(record,
						    "Decoding of universal"
						    " address failed\n");
						printf("\t\t%-s", record);
						sprintf(record, "%s:%s",
						    na->na_r_netid,
						    na->na_r_addr);
					}
					if (error == EADDRTRAN) {
						sprintf(record,
						    "IP address to "
						    "hostname"
						    " translation failed");
						printf("\t\t%-s", record);
						sprintf(record, "%s:%s:%s",
						    na->na_r_netid,
						    ipaddress, port);
					}
					printf("\t\t%-s", record);

					/*
					 * Do a NULL procedure ping to check
					 * the status of the data server.
					 */
					error = null_procedure_ping(
					    hostname,
					    na->na_r_netid,
					    &ds_status);
					if (error == 0) {
						record[strlen(record)] = ' ';
						if (ds_status == RPC_SUCCESS) {
							sprintf(record, " OK");
						} else {
							sprintf(record,
							    "FAILED (%s)",
							    clnt_sperrno(
							    ds_status));
						}
					} else {
						sprintf(record,
						    " STATUS UNKNOWN %d",
						    error);
						switch (error) {
						case ETLI:
							clnt_pcreateerror(
							    "\t\tnfsstat");
							break;
						case ENETCONF:
							fprintf(stderr,
							    "\t\tNetwork"
							    " address "
							    "error\n");
							break;
						case ENETADDR:
							netdir_perror(
							    "\t\tnfsstat");
							break;
						default:
							fprintf(stderr,
							    "Unknown "
							    "error\n");
						}
					}
					printf("%-s\n", record);
				}
			} else {
				sprintf(record, "Data server information"
				    " not available");
				printf("\t\t%-s\n", record);
				break;
			}
			i++;
		} while (i < los->plo_stripe_count);
		i = 0;
	}
	/*
	 * Free memory
	 */
	xdr_free((xdrproc_t)xdr_layoutstats_t, (char *)decoded_stats);
}

static int
getstats_rpc(void)
{
	int field_width = 0;

	if (rpc_clts_client_kstat != NULL) {
		safe_kstat_read(kc, rpc_clts_client_kstat, NULL);
		field_width = stat_width(rpc_clts_client_kstat, field_width);
	}

	if (rpc_cots_client_kstat != NULL) {
		safe_kstat_read(kc, rpc_cots_client_kstat, NULL);
		field_width = stat_width(rpc_cots_client_kstat, field_width);
	}

	if (rpc_rdma_client_kstat != NULL) {
		safe_kstat_read(kc, rpc_rdma_client_kstat, NULL);
		field_width = stat_width(rpc_rdma_client_kstat, field_width);
	}

	if (rpc_clts_server_kstat != NULL) {
		safe_kstat_read(kc, rpc_clts_server_kstat, NULL);
		field_width =  stat_width(rpc_clts_server_kstat, field_width);
	}
	if (rpc_cots_server_kstat != NULL) {
		safe_kstat_read(kc, rpc_cots_server_kstat, NULL);
		field_width = stat_width(rpc_cots_server_kstat, field_width);
	}
	if (rpc_rdma_server_kstat != NULL) {
		safe_kstat_read(kc, rpc_rdma_server_kstat, NULL);
		field_width = stat_width(rpc_rdma_server_kstat, field_width);
	}
	return (field_width);
}

static int
getstats_nfs(void)
{
	int field_width = 0;

	if (nfs_client_kstat != NULL) {
		safe_kstat_read(kc, nfs_client_kstat, NULL);
		field_width = stat_width(nfs_client_kstat, field_width);
	}
	if (nfs_debug_kstat != NULL) {
		safe_kstat_read(kc, nfs_debug_kstat, NULL);
		field_width = stat_width(nfs_debug_kstat, field_width);
	}
	if (nfs4_client_kstat != NULL) {
		safe_kstat_read(kc, nfs4_client_kstat, NULL);
		field_width = stat_width(nfs4_client_kstat, field_width);
	}
	if (nfs4_debug_kstat != NULL) {
		safe_kstat_read(kc, nfs4_debug_kstat, NULL);
		field_width = stat_width(nfs4_debug_kstat, field_width);
	}
	if (nfs41_client_kstat != NULL) {
		safe_kstat_read(kc, nfs41_client_kstat, NULL);
		field_width = stat_width(nfs41_client_kstat, field_width);
	}
	if (nfs_server_v2_kstat != NULL) {
		safe_kstat_read(kc, nfs_server_v2_kstat, NULL);
		field_width = stat_width(nfs_server_v2_kstat, field_width);
	}
	if (nfs_server_v3_kstat != NULL) {
		safe_kstat_read(kc, nfs_server_v3_kstat, NULL);
		field_width = stat_width(nfs_server_v3_kstat, field_width);
	}
	if (nfs_server_v4_kstat != NULL) {
		safe_kstat_read(kc, nfs_server_v4_kstat, NULL);
		field_width = stat_width(nfs_server_v4_kstat, field_width);
	}
	return (field_width);
}

static int
getstats_rfsproc(int ver)
{
	int field_width = 0;

	if ((ver == 2) && (rfsproccnt_v2_kstat != NULL)) {
		safe_kstat_read(kc, rfsproccnt_v2_kstat, NULL);
		field_width = req_width(rfsproccnt_v2_kstat, field_width);
	}
	if ((ver == 3) && (rfsproccnt_v3_kstat != NULL)) {
		safe_kstat_read(kc, rfsproccnt_v3_kstat, NULL);
		field_width = req_width(rfsproccnt_v3_kstat, field_width);
	}
	if ((ver == 4) && (rfsproccnt_v4_kstat != NULL)) {
		safe_kstat_read(kc, rfsproccnt_v4_kstat, NULL);
		field_width = req_width(rfsproccnt_v4_kstat, field_width);
	}
	return (field_width);
}

static int
getstats_rfsreq(int ver)
{
	int field_width = 0;
	if ((ver == 2) && (rfsreqcnt_v2_kstat != NULL)) {
		safe_kstat_read(kc, rfsreqcnt_v2_kstat, NULL);
		field_width = req_width(rfsreqcnt_v2_kstat, field_width);
	}
	if ((ver == 3) && (rfsreqcnt_v3_kstat != NULL)) {
		safe_kstat_read(kc, rfsreqcnt_v3_kstat, NULL);
		field_width = req_width(rfsreqcnt_v3_kstat,  field_width);
	}
	if ((ver == 4) && (rfsreqcnt_v4_kstat != NULL)) {
		safe_kstat_read(kc, rfsreqcnt_v4_kstat, NULL);
		field_width = req_width(rfsreqcnt_v4_kstat, field_width);
	}
	if ((ver == 41) && (rfsreqcnt_v41_kstat != NULL)) {
		safe_kstat_read(kc, rfsreqcnt_v41_kstat, NULL);
		field_width = req_width(rfsreqcnt_v41_kstat, field_width);
	}

	return (field_width);
}

static int
getstats_aclproc(void)
{
	int field_width = 0;
	if (aclproccnt_v2_kstat != NULL) {
		safe_kstat_read(kc, aclproccnt_v2_kstat, NULL);
		field_width = req_width(aclproccnt_v2_kstat, field_width);
	}
	if (aclproccnt_v3_kstat != NULL) {
		safe_kstat_read(kc, aclproccnt_v3_kstat, NULL);
		field_width = req_width(aclproccnt_v3_kstat, field_width);
	}
	return (field_width);
}

static int
getstats_aclreq(void)
{
	int field_width = 0;
	if (aclreqcnt_v2_kstat != NULL) {
		safe_kstat_read(kc, aclreqcnt_v2_kstat, NULL);
		field_width = req_width(aclreqcnt_v2_kstat, field_width);
	}
	if (aclreqcnt_v3_kstat != NULL) {
		safe_kstat_read(kc, aclreqcnt_v3_kstat, NULL);
		field_width = req_width(aclreqcnt_v3_kstat, field_width);
	}
	return (field_width);
}

static void
putstats(void)
{
	if (rpc_clts_client_kstat != NULL)
		safe_kstat_write(kc, rpc_clts_client_kstat, NULL);
	if (rpc_cots_client_kstat != NULL)
		safe_kstat_write(kc, rpc_cots_client_kstat, NULL);
	if (rpc_rdma_client_kstat != NULL)
		safe_kstat_write(kc, rpc_rdma_client_kstat, NULL);
	if (nfs_client_kstat != NULL)
		safe_kstat_write(kc, nfs_client_kstat, NULL);
	if (nfs_debug_kstat != NULL)
		safe_kstat_write(kc, nfs_debug_kstat, NULL);
	if (nfs4_client_kstat != NULL)
		safe_kstat_write(kc, nfs4_client_kstat, NULL);
	if (nfs4_debug_kstat != NULL)
		safe_kstat_write(kc, nfs4_debug_kstat, NULL);
	if (nfs41_client_kstat != NULL)
		safe_kstat_write(kc, nfs41_client_kstat, NULL);
	if (rpc_clts_server_kstat != NULL)
		safe_kstat_write(kc, rpc_clts_server_kstat, NULL);
	if (rpc_cots_server_kstat != NULL)
		safe_kstat_write(kc, rpc_cots_server_kstat, NULL);
	if (rpc_rdma_server_kstat != NULL)
		safe_kstat_write(kc, rpc_rdma_server_kstat, NULL);
	if (nfs_server_v2_kstat != NULL)
		safe_kstat_write(kc, nfs_server_v2_kstat, NULL);
	if (nfs_server_v3_kstat != NULL)
		safe_kstat_write(kc, nfs_server_v3_kstat, NULL);
	if (nfs_server_v4_kstat != NULL)
		safe_kstat_write(kc, nfs_server_v4_kstat, NULL);
	if (rfsproccnt_v2_kstat != NULL)
		safe_kstat_write(kc, rfsproccnt_v2_kstat, NULL);
	if (rfsproccnt_v3_kstat != NULL)
		safe_kstat_write(kc, rfsproccnt_v3_kstat, NULL);
	if (rfsproccnt_v4_kstat != NULL)
		safe_kstat_write(kc, rfsproccnt_v4_kstat, NULL);
	if (rfsreqcnt_v2_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v2_kstat, NULL);
	if (rfsreqcnt_v3_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v3_kstat, NULL);
	if (rfsreqcnt_v4_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v4_kstat, NULL);
	if (rfsreqcnt_v41_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v41_kstat, NULL);
	if (aclproccnt_v2_kstat != NULL)
		safe_kstat_write(kc, aclproccnt_v2_kstat, NULL);
	if (aclproccnt_v3_kstat != NULL)
		safe_kstat_write(kc, aclproccnt_v3_kstat, NULL);
	if (aclreqcnt_v2_kstat != NULL)
		safe_kstat_write(kc, aclreqcnt_v2_kstat, NULL);
	if (aclreqcnt_v3_kstat != NULL)
		safe_kstat_write(kc, aclreqcnt_v3_kstat, NULL);
}

static void
setup(void)
{
	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");

	/* alloc space for our temporary kstat */
	safe_zalloc((void **)&ksum_kstat, sizeof (kstat_t), 0);
	/*
	 * kstat_sum function assumes implicitly that ks_data is NULL. It may
	 * not always be the case.
	 */
	ksum_kstat->ks_data = NULL;
	rpc_clts_client_kstat = kstat_lookup(kc, "unix", 0, "rpc_clts_client");
	rpc_clts_server_kstat = kstat_lookup(kc, "unix", 0, "rpc_clts_server");
	rpc_cots_client_kstat = kstat_lookup(kc, "unix", 0, "rpc_cots_client");
	rpc_cots_server_kstat = kstat_lookup(kc, "unix", 0, "rpc_cots_server");
	rpc_rdma_client_kstat = kstat_lookup(kc, "unix", 0, "rpc_rdma_client");
	rpc_rdma_server_kstat = kstat_lookup(kc, "unix", 0, "rpc_rdma_server");
	nfs_client_kstat = kstat_lookup(kc, "nfs", 0, "nfs_client");
	nfs4_client_kstat = kstat_lookup(kc, "nfs", 0, "nfs4_client");
	/*
	 * Holds the debug stats as the sum across the minor versions for v4.
	 * No easy way to separate them in the kernel on the basis of minor
	 * version as of now.
	 */
	nfs4_debug_kstat = kstat_lookup(kc, "nfs", 0, "nfs4_client_debug");
	nfs_debug_kstat = kstat_lookup(kc, "nfs", 0, "nfs_client_debug");
	nfs41_client_kstat = kstat_lookup(kc, "nfs", 0, "nfs41_client");
	nfs_server_v2_kstat = kstat_lookup(kc, "nfs", 2, "nfs_server");
	nfs_server_v3_kstat = kstat_lookup(kc, "nfs", 3, "nfs_server");
	nfs_server_v4_kstat = kstat_lookup(kc, "nfs", 4, "nfs_server");
	rfsproccnt_v2_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v2");
	rfsproccnt_v3_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v3");
	rfsproccnt_v4_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v4");
	rfsproccnt_v41_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v41");
	rfsreqcnt_v2_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v2");
	rfsreqcnt_v3_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v3");
	rfsreqcnt_v4_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v4");
	rfsreqcnt_v41_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v41");
	aclproccnt_v2_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclproccnt_v2");
	aclproccnt_v3_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclproccnt_v3");
	aclreqcnt_v2_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclreqcnt_v2");
	aclreqcnt_v3_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclreqcnt_v3");
	if (rpc_clts_client_kstat == NULL && rpc_cots_server_kstat == NULL &&
	    rfsproccnt_v2_kstat == NULL && rfsreqcnt_v3_kstat == NULL)
		fail(0, "Multiple kstat lookups failed."
		    "Your kernel module may not be loaded\n");
}

static int
req_width(kstat_t *req, int field_width)
{
	int i, nreq, per, len;
	char fixlen[128];
	kstat_named_t *knp;
	uint64_t tot;

	tot = 0;
	knp = KSTAT_NAMED_PTR(req);
	for (i = 0; i < req->ks_ndata; i++)
		tot += knp[i].value.ui64;

	knp = kstat_data_lookup(req, "null");
	nreq = req->ks_ndata - (knp - KSTAT_NAMED_PTR(req));

	for (i = 0; i < nreq; i++) {
		len = strlen(knp[i].name) + 1;
		if (field_width < len)
			field_width = len;
		if (tot)
			per = (int)(knp[i].value.ui64 * 100 / tot);
		else
			per = 0;
		(void) sprintf(fixlen, "%" PRIu64 " %d%%",
		    knp[i].value.ui64, per);
		len = strlen(fixlen) + 1;
		if (field_width < len)
			field_width = len;
	}
	return (field_width);
}

static int
stat_width(kstat_t *req, int field_width)
{
	int i, nreq, len;
	char fixlen[128];
	kstat_named_t *knp;

	knp = KSTAT_NAMED_PTR(req);
	nreq = req->ks_ndata;

	for (i = 0; i < nreq; i++) {
		len = strlen(knp[i].name) + 1;
		if (field_width < len)
			field_width = len;
		(void) sprintf(fixlen, "%" PRIu64, knp[i].value.ui64);
		len = strlen(fixlen) + 1;
		if (field_width < len)
			field_width = len;
	}
	return (field_width);
}

static void
cr_print(int zflag)
{
	int field_width;

	field_width = getstats_rpc();
	if (field_width == 0)
		return;

	stat_print("\nClient rpc:\nConnection oriented:",
	    rpc_cots_client_kstat,
	    &old_rpc_cots_client_kstat.kst, field_width, zflag);
	stat_print("Connectionless:", rpc_clts_client_kstat,
	    &old_rpc_clts_client_kstat.kst, field_width, zflag);
	stat_print("RDMA based:", rpc_rdma_client_kstat,
	    &old_rpc_rdma_client_kstat.kst, field_width, zflag);
}

static void
sr_print(int zflag)
{
	int field_width;

	field_width = getstats_rpc();
	if (field_width == 0)
		return;

	stat_print("\nServer rpc:\nConnection oriented:", rpc_cots_server_kstat,
	    &old_rpc_cots_server_kstat.kst, field_width, zflag);
	stat_print("Connectionless:", rpc_clts_server_kstat,
	    &old_rpc_clts_server_kstat.kst, field_width, zflag);
	stat_print("RDMA based:", rpc_rdma_server_kstat,
	    &old_rpc_rdma_server_kstat.kst, field_width, zflag);
}

static void
cn_print(int zflag, int vflag)
{
	int field_width;

	field_width = getstats_nfs();
	if (field_width == 0)
		return;

	if (vflag == 0) {
		nfsstat_kstat_sum(nfs_client_kstat, nfs4_client_kstat,
		    nfs41_client_kstat, ksum_kstat);
		stat_print("\nClient nfs (sum):", ksum_kstat,
		    &old_ksum_kstat.kst, field_width, zflag);
		if (zflag) {
			/*
			 * The following statements ensure that the
			 * individual client counts are also zeroed when
			 * -z flag is issued without any version.
			 */
			stat_print("\nClient nfs (v2/3):", nfs_client_kstat,
			    &old_nfs_client_kstat.kst, field_width, zflag);
			stat_print("\nClient nfs (v4):", nfs4_client_kstat,
			    &old_nfs4_client_kstat.kst, field_width, zflag);
			stat_print("\nClient nfs (v41):", nfs41_client_kstat,
			    &old_nfs41_client_kstat.kst, field_width, zflag);
		}
	}

	if (vflag == 2 || vflag == 3) {
		stat_print("\nClient nfs:", nfs_client_kstat,
		    &old_nfs_client_kstat.kst, field_width, zflag);
		if (nfs_debug_kstat != NULL) {
		stat_print("\nClient nfs debug statistics:", nfs_debug_kstat,
		    &old_nfs_debug_kstat.kst, field_width, zflag);
		}
	}

	if (vflag == 4) {
		stat_print("\nClient nfs:", nfs4_client_kstat,
		    &old_nfs4_client_kstat.kst, field_width, zflag);
		if (nfs4_debug_kstat != NULL) {
			stat_print("\nClient nfs debug statistics "
			    "across minor versions:", nfs4_debug_kstat,
			    &old_nfs4_debug_kstat.kst,
			    field_width, zflag);
		}
	}

	if (vflag == 41) {
		stat_print("\nClient nfs:", nfs41_client_kstat,
		    &old_nfs41_client_kstat.kst, field_width, zflag);
	}

	if (vflag == 2 || vflag == 0) {
		field_width = getstats_rfsreq(2);
		req_print(rfsreqcnt_v2_kstat, &old_rfsreqcnt_v2_kstat.kst,
		    2, field_width, zflag);
	}

	if (vflag == 3 || vflag == 0) {
		field_width = getstats_rfsreq(3);
		req_print(rfsreqcnt_v3_kstat, &old_rfsreqcnt_v3_kstat.kst,
		    3, field_width, zflag);
	}

	if (vflag == 4 || vflag == 0) {
		field_width = getstats_rfsreq(4);
		req_print_v4(rfsreqcnt_v4_kstat, &old_rfsreqcnt_v4_kstat.kst,
		    4, field_width, zflag);
	}

	if (vflag == 41 || vflag == 0) {
		field_width = getstats_rfsreq(41);
		req_print_v4(rfsreqcnt_v41_kstat, &old_rfsreqcnt_v41_kstat.kst,
		    41, field_width, zflag);
	}

}

static void
sn_print(int zflag, int vflag)
{
	int  field_width;

	field_width = getstats_nfs();
	if (field_width == 0)
		return;

	if (vflag == 2 || vflag == 0) {
		stat_print("\nServer NFSv2:", nfs_server_v2_kstat,
		    &old_nfs_server_v2_kstat.kst, field_width, zflag);
	}

	if (vflag == 3 || vflag == 0) {
		stat_print("\nServer NFSv3:", nfs_server_v3_kstat,
		    &old_nfs_server_v3_kstat.kst, field_width, zflag);
	}

	if (vflag == 4 || vflag == 0) {
		stat_print("\nServer NFSv4:", nfs_server_v4_kstat,
		    &old_nfs_server_v4_kstat.kst, field_width, zflag);
	}

	if (vflag == 2 || vflag == 0) {
		field_width = getstats_rfsproc(2);
		req_print(rfsproccnt_v2_kstat, &old_rfsproccnt_v2_kstat.kst,
		    2, field_width, zflag);
	}

	if (vflag == 3 || vflag == 0) {
		field_width = getstats_rfsproc(3);
		req_print(rfsproccnt_v3_kstat, &old_rfsproccnt_v3_kstat.kst,
		    3, field_width, zflag);
	}

	if (vflag == 4 || vflag == 0) {
		field_width = getstats_rfsproc(4);
		req_print_v4(rfsproccnt_v4_kstat, &old_rfsproccnt_v4_kstat.kst,
		    4, field_width, zflag);
	}
}

static void
ca_print(int zflag, int vflag)
{
	int  field_width;

	field_width = getstats_aclreq();
	if (field_width == 0)
		return;

	printf("\nClient nfs_acl:\n");

	if (vflag == 2 || vflag == 0) {
		req_print(aclreqcnt_v2_kstat, &old_aclreqcnt_v2_kstat.kst, 2,
		    field_width, zflag);
	}

	if (vflag == 3 || vflag == 0) {
		req_print(aclreqcnt_v3_kstat, &old_aclreqcnt_v3_kstat.kst,
		    3, field_width, zflag);
	}
}

static void
sa_print(int zflag, int vflag)
{
	int  field_width;

	field_width = getstats_aclproc();
	if (field_width == 0)
		return;

	printf("\nServer nfs_acl:\n");

	if (vflag == 2 || vflag == 0) {
		req_print(aclproccnt_v2_kstat, &old_aclproccnt_v2_kstat.kst,
		    2, field_width, zflag);
	}

	if (vflag == 3 || vflag == 0) {
		req_print(aclproccnt_v3_kstat, &old_aclproccnt_v3_kstat.kst,
		    3, field_width, zflag);
	}
}

#define	MIN(a, b)	((a) < (b) ? (a) : (b))

static void
req_print(kstat_t *req, kstat_t *req_old, int ver, int field_width,
    int zflag)
{
	int i, j, nreq, per, ncolumns;
	uint64_t tot, old_tot;
	char fixlen[128];
	kstat_named_t *knp;
	kstat_named_t *kptr;
	kstat_named_t *knp_old;

	if (req == NULL)
		return;

	if (field_width == 0)
		return;

	ncolumns = (MAX_COLUMNS -1)/field_width;
	knp = kstat_data_lookup(req, "null");
	knp_old = KSTAT_NAMED_PTR(req_old);

	kptr = KSTAT_NAMED_PTR(req);
	nreq = req->ks_ndata - (knp - KSTAT_NAMED_PTR(req));

	tot = 0;
	old_tot = 0;

	if (knp_old == NULL) {
		old_tot = 0;
	}

	for (i = 0; i < req->ks_ndata; i++)
		tot += kptr[i].value.ui64;

	if (interval && knp_old != NULL) {
		for (i = 0; i < req_old->ks_ndata; i++)
			old_tot += knp_old[i].value.ui64;
		tot -= old_tot;
	}

	printf("Version %d: (%" PRIu64 " calls)\n", ver, tot);

	for (i = 0; i < nreq; i += ncolumns) {
		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			printf("%-*s", field_width, knp[j].name);
		}
		printf("\n");
		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			if (tot && interval && knp_old != NULL)
				per = (int)((knp[j].value.ui64 -
				    knp_old[j].value.ui64) * 100 / tot);
			else if (tot)
				per = (int)(knp[j].value.ui64 * 100 / tot);
			else
				per = 0;
			(void) sprintf(fixlen, "%" PRIu64 " %d%% ",
			    ((interval && knp_old != NULL) ?
			    (knp[j].value.ui64 - knp_old[j].value.ui64)
			    : knp[j].value.ui64), per);
			printf("%-*s", field_width, fixlen);
		}
		printf("\n");
	}
	if (zflag) {
		for (i = 0; i < req->ks_ndata; i++)
			knp[i].value.ui64 = 0;
	}
	if (knp_old != NULL)
		nfsstat_kstat_copy(req, req_old, 1);
	else
		nfsstat_kstat_copy(req, req_old, 0);
}

/*
 * Separate version of the req_print() to deal with V4 and its use of
 * procedures and operations.  It looks odd to have the counts for
 * both of those lumped into the same set of statistics so this
 * function (copy of req_print() does the separation and titles).
 */

#define	COUNT	2

static void
req_print_v4
(kstat_t *req, kstat_t *req_old, int ver, int field_width, int zflag)
{
	int i, j, nreq, per, ncolumns;
	uint64_t tot, tot_ops, old_tot, old_tot_ops;
	char fixlen[128];
	kstat_named_t *kptr;
	kstat_named_t *knp;
	kstat_named_t *kptr_old;
	nfs_opnum4 op_toskip;
	int skip_count = 0;

	if (req == NULL)
		return;

	if (field_width == 0)
		return;

	ncolumns = (MAX_COLUMNS)/field_width;
	kptr = KSTAT_NAMED_PTR(req);
	kptr_old = KSTAT_NAMED_PTR(req_old);

	if (kptr_old == NULL) {
		old_tot_ops = 0;
		old_tot = 0;
	} else {
		old_tot =  kptr_old[0].value.ui64 + kptr_old[1].value.ui64;
		for (i = 2, old_tot_ops = 0; i < req_old->ks_ndata; i++)
			old_tot_ops += kptr_old[i].value.ui64;
	}

	/* Count the number of operations sent */
	for (i = 2, tot_ops = 0; i < req->ks_ndata; i++)
		tot_ops += kptr[i].value.ui64;
	/* For v4 NULL/COMPOUND are the only procedures */
	tot = kptr[0].value.ui64 + kptr[1].value.ui64;

	if (interval) {
		tot -= old_tot;
		tot_ops -= old_tot_ops;
	}

	printf("Version %d: (%" PRIu64 " calls)\n", ver, tot);

	knp = kstat_data_lookup(req, "null");
	nreq = req->ks_ndata - (knp - KSTAT_NAMED_PTR(req));

	for (i = 0; i < COUNT; i += ncolumns) {
		for (j = i; j < MIN(i + ncolumns, 2); j++) {
			printf("%-*s", field_width, knp[j].name);
		}
		printf("\n");
		for (j = i; j < MIN(i + ncolumns, 2); j++) {
			if (tot && interval && kptr_old != NULL)
				per = (int)((knp[j].value.ui64 -
				    kptr_old[j].value.ui64) * 100 / tot);
			else if (tot)
				per = (int)(knp[j].value.ui64 * 100 / tot);
			else
				per = 0;
			(void) sprintf(fixlen, "%" PRIu64 " %d%% ",
			    ((interval && kptr_old != NULL) ?
			    (knp[j].value.ui64 - kptr_old[j].value.ui64)
			    : knp[j].value.ui64), per);
			printf("%-*s", field_width, fixlen);
		}
		printf("\n");
	}

	op_toskip = ops_toskip_v41[skip_count];
	printf("Version %d: (%" PRIu64 " operations)\n", ver, tot_ops);
	for (i = 2; i < nreq; i += ncolumns) {
		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			printf("%-*s", field_width, knp[j].name);
		}
		printf("\n");


		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			if (tot_ops && interval && kptr_old != NULL)
				per = (int)((knp[j].value.ui64 -
				    kptr_old[j].value.ui64) * 100 / tot_ops);
			else if (tot_ops)
				per = (int)(knp[j].value.ui64 * 100 / tot_ops);
			else
				per = 0;
			if (ver == 41 && j == op_toskip) {
				(void) sprintf(fixlen, "%" PRIu64 " n/a",
				    ((interval && kptr_old != NULL) ?
				    (knp[j].value.ui64 - kptr_old[j].value.ui64)
				    : knp[j].value.ui64), per);
				skip_count++;
				if (skip_count < NUM_OPS_TOSKIP) {
					op_toskip = ops_toskip_v41[skip_count];
				}
			} else {
				(void) sprintf(fixlen, "%" PRIu64 " %d%% ",
				    ((interval && kptr_old != NULL) ?
				    (knp[j].value.ui64 - kptr_old[j].value.ui64)
				    : knp[j].value.ui64), per);
			}
			printf("%-*s", field_width, fixlen);
		}
		printf("\n");
	}
	if (zflag) {
		for (i = 0; i < req->ks_ndata; i++)
			kptr[i].value.ui64 = 0;
	}
	if (kptr_old != NULL)
		nfsstat_kstat_copy(req, req_old, 1);
	else
		nfsstat_kstat_copy(req, req_old, 0);
}

static void
stat_print(const char *title_string, kstat_t *req, kstat_t  *req_old,
    int field_width, int zflag)
{
	int i, j, nreq, ncolumns;
	char fixlen[128];
	kstat_named_t *knp;
	kstat_named_t *knp_old;

	if (req == NULL)
		return;

	if (field_width == 0)
		return;

	printf("%s\n", title_string);
	ncolumns = (MAX_COLUMNS -1)/field_width;

	/* MEANS knp =  (kstat_named_t *)req->ks_data */
	knp = KSTAT_NAMED_PTR(req);
	nreq = req->ks_ndata;
	knp_old = KSTAT_NAMED_PTR(req_old);

	for (i = 0; i < nreq; i += ncolumns) {
		/* prints out the titles of the columns */
		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			printf("%-*s", field_width, knp[j].name);
		}
		printf("\n");
		/* prints out the stat numbers */
		for (j = i; j < MIN(i + ncolumns, nreq); j++) {
			(void) sprintf(fixlen, "%" PRIu64 " ",
			    (interval && knp_old != NULL) ?
			    (knp[j].value.ui64 - knp_old[j].value.ui64)
			    : knp[j].value.ui64);
			printf("%-*s", field_width, fixlen);
		}
		printf("\n");

	}
	if (zflag) {
		for (i = 0; i < req->ks_ndata; i++)
			knp[i].value.ui64 = 0;
	}

	if (knp_old != NULL)
		nfsstat_kstat_copy(req, req_old, 1);
	else
		nfsstat_kstat_copy(req, req_old, 0);
}

static void
nfsstat_kstat_sum(kstat_t *kstat1, kstat_t *kstat2, kstat_t *kstat3,
    kstat_t *sum)
{
	int i;
	kstat_named_t *knp1, *knp2, *knp3, *knpsum;

	if (kstat1 == NULL || kstat2 == NULL || kstat3 == NULL) {
		return;
	}

	knp1 = KSTAT_NAMED_PTR(kstat1);
	knp2 = KSTAT_NAMED_PTR(kstat2);
	knp3 = KSTAT_NAMED_PTR(kstat3);

	if (sum->ks_data == NULL) {
		nfsstat_kstat_copy(kstat1, sum, 0);
	}
	knpsum = KSTAT_NAMED_PTR(sum);

	for (i = 0; i < (kstat1->ks_ndata); i++) {
		knpsum[i].value.ui64 =  knp1[i].value.ui64 +
		    knp2[i].value.ui64 + knp3[i].value.ui64;
	}
}

/*
 * my_dir and my_path could be pointers
 */
struct myrec {
	ulong_t my_fsid;
	char my_dir[MAXPATHLEN];
	char *my_path;
	char *ig_path;
	struct myrec *next;
};

/*
 * Print the mount table info
 */
static void
mi_print(void)
{
	FILE *mt;
	struct extmnttab m;
	struct myrec *list, *mrp, *pmrp;
	char *flavor;
	int ignored = 0;
	seconfig_t nfs_sec;
	kstat_t *ksp;
	struct mntinfo_kstat mik;
	int transport_flag = 0;
	int path_count;
	int found;
	char *timer_name[] = {
		"Lookups",
		"Reads",
		"Writes",
		"All"
	};

	mt = fopen(MNTTAB, "r");
	if (mt == NULL) {
		perror(MNTTAB);
		exit(0);
	}

	list = NULL;
	resetmnttab(mt);

	while (getextmntent(mt, &m, sizeof (struct extmnttab)) == 0) {
		/* ignore non "nfs" and save the "ignore" entries */
		if (strcmp(m.mnt_fstype, MNTTYPE_NFS) != 0)
			continue;
		/*
		 * Check to see here if user gave a path(s) to
		 * only show the mount point they wanted
		 * Iterate through the list of paths the user gave and see
		 * if any of them match our current nfs mount
		 */
		if (path[0] != NULL) {
			found = 0;
			for (path_count = 0; path[path_count] != NULL;
			    path_count++) {
				if (strcmp(path[path_count], m.mnt_mountp)
				    == 0) {
					found = 1;
					break;
				}
			}
			if (!found)
				continue;
		}

		if ((mrp = malloc(sizeof (struct myrec))) == 0) {
			fprintf(stderr, "nfsstat: not enough memory\n");
			exit(1);
		}
		mrp->my_fsid = makedev(m.mnt_major, m.mnt_minor);
		if (ignore(m.mnt_mntopts)) {
			/*
			 * ignored entries cannot be ignored for this
			 * option. We have to display the info for this
			 * nfs mount. The ignore is an indication
			 * that the actual mount point is different and
			 * something is in between the nfs mount.
			 * So save the mount point now
			 */
			if ((mrp->ig_path = malloc(
			    strlen(m.mnt_mountp) + 1)) == 0) {
				fprintf(stderr, "nfsstat: not enough memory\n");
				exit(1);
			}
			(void) strcpy(mrp->ig_path, m.mnt_mountp);
			ignored++;
		} else {
			mrp->ig_path = 0;
			(void) strcpy(mrp->my_dir, m.mnt_mountp);
		}
		if ((mrp->my_path = strdup(m.mnt_special)) == NULL) {
			fprintf(stderr, "nfsstat: not enough memory\n");
			exit(1);
		}
		mrp->next = list;
		list = mrp;
	}

	/*
	 * If something got ignored, go to the beginning of the mnttab
	 * and look for the cachefs entries since they are the one
	 * causing this. The mount point saved for the ignored entries
	 * is matched against the special to get the actual mount point.
	 * We are interested in the acutal mount point so that the output
	 * look nice too.
	 */
	if (ignored) {
		rewind(mt);
		resetmnttab(mt);
		while (getextmntent(mt, &m, sizeof (struct extmnttab)) == 0) {

			/* ignore non "cachefs" */
			if (strcmp(m.mnt_fstype, MNTTYPE_CACHEFS) != 0)
				continue;

			for (mrp = list; mrp; mrp = mrp->next) {
				if (mrp->ig_path == 0)
					continue;
				if (strcmp(mrp->ig_path, m.mnt_special) == 0) {
					mrp->ig_path = 0;
					(void) strcpy(mrp->my_dir,
					    m.mnt_mountp);
				}
			}
		}
		/*
		 * Now ignored entries which do not have
		 * the my_dir initialized are really ignored; This never
		 * happens unless the mnttab is corrupted.
		 */
		for (pmrp = 0, mrp = list; mrp; mrp = mrp->next) {
			if (mrp->ig_path == 0)
				pmrp = mrp;
			else if (pmrp)
				pmrp->next = mrp->next;
			else
				list = mrp->next;
		}
	}

	(void) fclose(mt);


	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		int i;

		if (ksp->ks_type != KSTAT_TYPE_RAW)
			continue;
		if (strcmp(ksp->ks_module, "nfs") != 0)
			continue;
		if (strcmp(ksp->ks_name, "mntinfo") != 0)
			continue;

		for (mrp = list; mrp; mrp = mrp->next) {
			if ((mrp->my_fsid & MAXMIN) == ksp->ks_instance)
				break;
		}
		if (mrp == 0)
			continue;

		if (safe_kstat_read(kc, ksp, &mik) == -1)
			continue;

		printf("%s from %s\n", mrp->my_dir, mrp->my_path);

		/*
		 * for printing rdma transport and provider string.
		 * This way we avoid modifying the kernel mntinfo_kstat
		 * struct for protofmly.
		 */
		if (strcmp(mik.mik_proto, "ibtf") == 0) {
			printf(" Flags:		vers=%u,proto=rdma",
			    mik.mik_vers);
			transport_flag = 1;
		} else {
			printf(" Flags:		vers=%u,proto=%s",
			    mik.mik_vers, mik.mik_proto);
			transport_flag = 0;
		}

		/*
		 *  get the secmode name from /etc/nfssec.conf.
		 */
		if (!nfs_getseconfig_bynumber(mik.mik_secmod, &nfs_sec)) {
			flavor = nfs_sec.sc_name;
		} else
			flavor = NULL;

		if (flavor != NULL)
			printf(",sec=%s", flavor);
		else
			printf(",sec#=%d", mik.mik_secmod);

		printf(",%s", (mik.mik_flags & MI_HARD) ? "hard" : "soft");
		if (mik.mik_flags & MI_PRINTED)
			printf(",printed");
		printf(",%s", (mik.mik_flags & MI_INT) ? "intr" : "nointr");
		if (mik.mik_flags & MI_DOWN)
			printf(",down");
		if (mik.mik_flags & MI_NOAC)
			printf(",noac");
		if (mik.mik_flags & MI_NOCTO)
			printf(",nocto");
		if (mik.mik_flags & MI_DYNAMIC)
			printf(",dynamic");
		if (mik.mik_flags & MI_LLOCK)
			printf(",llock");
		if (mik.mik_flags & MI_GRPID)
			printf(",grpid");
		if (mik.mik_flags & MI_RPCTIMESYNC)
			printf(",rpctimesync");
		if (mik.mik_flags & MI_LINK)
			printf(",link");
		if (mik.mik_flags & MI_SYMLINK)
			printf(",symlink");
		if (mik.mik_vers < NFS_V4 && mik.mik_flags & MI_READDIRONLY)
			printf(",readdironly");
		if (mik.mik_flags & MI_ACL)
			printf(",acl");
		if (mik.mik_flags & MI_DIRECTIO)
			printf(",forcedirectio");

		if (mik.mik_vers >= NFS_V4) {
			if (mik.mik_flags & MI4_MIRRORMOUNT)
				printf(",mirrormount");
		}

		printf(",rsize=%d,wsize=%d,retrans=%d,timeo=%d",
		    mik.mik_curread, mik.mik_curwrite, mik.mik_retrans,
		    mik.mik_timeo);
		printf("\n");
		printf(" Attr cache:	acregmin=%d,acregmax=%d"
		    ",acdirmin=%d,acdirmax=%d\n", mik.mik_acregmin,
		    mik.mik_acregmax, mik.mik_acdirmin, mik.mik_acdirmax);

		if (transport_flag) {
			printf(" Transport:	proto=rdma, plugin=%s\n",
			    mik.mik_proto);
		}

#define	srtt_to_ms(x) x, (x * 2 + x / 2)
#define	dev_to_ms(x) x, (x * 5)

		for (i = 0; i < NFS_CALLTYPES + 1; i++) {
			int j;

			j = (i == NFS_CALLTYPES ? i - 1 : i);
			if (mik.mik_timers[j].srtt ||
			    mik.mik_timers[j].rtxcur) {
				printf(" %s:     srtt=%d (%dms), "
				    "dev=%d (%dms), cur=%u (%ums)\n",
				    timer_name[i],
				    srtt_to_ms(mik.mik_timers[i].srtt),
				    dev_to_ms(mik.mik_timers[i].deviate),
				    mik.mik_timers[i].rtxcur,
				    mik.mik_timers[i].rtxcur * 20);
			}
		}

		if (strchr(mrp->my_path, ','))
			printf(
			    " Failover:	noresponse=%d,failover=%d,"
			    "remap=%d,currserver=%s\n",
			    mik.mik_noresponse, mik.mik_failover,
			    mik.mik_remap, mik.mik_curserver);
		printf("\n");
	}
}

static char *mntopts[] = { MNTOPT_IGNORE, MNTOPT_DEV, NULL };
#define	IGNORE  0
#define	DEV	1

/*
 * Return 1 if "ignore" appears in the options string
 */
static int
ignore(char *opts)
{
	char *value;
	char *s;

	if (opts == NULL)
		return (0);
	s = strdup(opts);
	if (s == NULL)
		return (0);
	opts = s;

	while (*opts != '\0') {
		if (getsubopt(&opts, mntopts, &value) == IGNORE) {
			free(s);
			return (1);
		}
	}

	free(s);
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "Usage: nfsstat [-cnrsza [-v version] "
	    "[-T d|u] [interval [count]]\n");
	fprintf(stderr, "Usage: nfsstat -m [pathname..]\n");
	fprintf(stderr, "Usage: nfsstat -l filename\n");
	exit(1);
}

void
fail(int do_perror, char *message, ...)
{
	va_list args;

	va_start(args, message);
	fprintf(stderr, "nfsstat: ");
	vfprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	exit(1);
}

kid_t
safe_kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_read(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
	return (kstat_chain_id);
}

kid_t
safe_kstat_write(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = 0;

	if (ksp->ks_data != NULL) {
		kstat_chain_id = kstat_write(kc, ksp, data);

		if (kstat_chain_id == -1)
			fail(1, "kstat_write(%x, '%s') failed", kc,
			    ksp->ks_name);
	}
	return (kstat_chain_id);
}

void
stats_timer(int interval)
{
	timer_t t_id;
	itimerspec_t time_struct;
	struct sigevent sig_struct;
	struct sigaction act;

	bzero(&sig_struct, sizeof (struct sigevent));
	bzero(&act, sizeof (struct sigaction));

	/* Create timer */
	sig_struct.sigev_notify = SIGEV_SIGNAL;
	sig_struct.sigev_signo = SIGUSR1;
	sig_struct.sigev_value.sival_int = 0;

	if (timer_create(CLOCK_REALTIME, &sig_struct, &t_id) != 0) {
		fail(1, "Timer creation failed");
	}

	act.sa_handler = handle_sig;

	if (sigaction(SIGUSR1, &act, NULL) != 0) {
		fail(1, "Could not set up signal handler");
	}

	time_struct.it_value.tv_sec = interval;
	time_struct.it_value.tv_nsec = 0;
	time_struct.it_interval.tv_sec = interval;
	time_struct.it_interval.tv_nsec = 0;

	/* Arm timer */
	if ((timer_settime(t_id, 0, &time_struct, NULL)) != 0) {
		fail(1, "Setting timer failed");
	}
}

void
handle_sig(int x)
{
}

static void
nfsstat_kstat_copy(kstat_t *src, kstat_t *dst, int fr)
{

	if (fr)
		free(dst->ks_data);

	*dst = *src;

	if (src->ks_data != NULL) {
		safe_zalloc(&dst->ks_data, src->ks_data_size, 0);
		(void) memcpy(dst->ks_data, src->ks_data, src->ks_data_size);
	} else {
		dst->ks_data = NULL;
		dst->ks_data_size = 0;
	}
}

/*
 * "Safe" allocators - if we return we're guaranteed to have the desired space
 * allocated and zero-filled. We exit via fail if we can't get the space.
 */
void
safe_zalloc(void **ptr, uint_t size, int free_first)
{
	if (ptr == NULL)
		fail(1, "invalid pointer");
	if (free_first && *ptr != NULL)
		free(*ptr);
	if ((*ptr = (void *)malloc(size)) == NULL)
		fail(1, "malloc failed");
	(void) memset(*ptr, 0, size);
}

static int
safe_strtoi(char const *val, char *errmsg)
{
	char *end;
	long tmp;
	errno = 0;
	tmp = strtol(val, &end, 10);
	if (*end != '\0' || errno)
		fail(0, "%s %s", errmsg, val);
	return ((int)tmp);
}
