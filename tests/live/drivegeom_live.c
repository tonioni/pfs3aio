/* LIVE @src volume.c @func GetDriveGeometry
 *
 * dg_TotalSectors of the DosEnvec-derived geometry is Cylinders * CylSectors in
 * 32 bits. A wrapped product would make a huge disk look small and defeat the
 * MAXDISKSIZE check of FDSFormat, so the driver saturates at 0xFFFFFFFF.
 * Lifts the checkout's real GetDriveGeometry (no trackdisk geometry, no
 * superfloppy: the plain DosEnvec branch); SetPartitionLimits is a mock.
 *
 *  - normal geometry: Cylinders * CylSectors, other fields copied unchanged;
 *  - saturation exactly where the product first reaches 2^32 (diff =
 *    UpperCyl-LowCyl >= 0xFFFFFFFF/CylSectors), and not one cylinder earlier;
 *  - the test uses UpperCyl-LowCyl, not UpperCyl alone;
 *  - CylSectors 0 (no heads or no blocks per track) must not divide by zero and
 *    gives the plain product (0), also for a huge cylinder count.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

typedef uint32_t ULONG; typedef uint32_t IPTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DB(x)

/* dos/filehandler.h */
#define DE_SIZEBLOCK 1
#define DE_NUMHEADS 3
#define DE_BLKSPERTRACK 5
#define DE_INTERLEAVE 8
#define DE_LOWCYL 9
#define DE_UPPERCYL 10
#define DE_MEMBUFTYPE 12
#define DG_UNKNOWN 31
#define DEF_SCSIDIRECT (1 << 16)
#define DEF_SUPERFLOPPY (1 << 17)

struct DriveGeometry
{
	ULONG dg_SectorSize, dg_TotalSectors, dg_Cylinders, dg_CylSectors, dg_Heads, dg_TrackSectors;
	ULONG dg_BufMemType;
	uint8_t dg_DeviceType, dg_Flags;
};
struct DosEnvec { ULONG de[16]; };
typedef struct globaldata { struct DosEnvec *dosenvec; struct DriveGeometry *geom; } globaldata;

static int nlimits;
static void SetPartitionLimits(globaldata *g) { nlimits++; }
static BOOL get_scsi_geometry(globaldata *g) { return FALSE; }
static void UpdateDosEnvec(globaldata *g) { }

#include "gen/GetDriveGeometry.c"            /* the REAL function from ../../volume.c */

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static void on_fpe(int sig)
{
	static const char m[] = "  FAIL: division by zero (CylSectors 0)\n";
	(void)sig;
	write(1, m, sizeof m - 1);
	_exit(1);
}

static struct DosEnvec env;
static struct DriveGeometry geo;
static globaldata g = { &env, &geo };

static void geometry(const char *what, ULONG low, ULONG upper, ULONG heads, ULONG bpt, ULONG want_total)
{
	ULONG cs = heads * bpt;

	env.de[DE_SIZEBLOCK] = 128; env.de[DE_NUMHEADS] = heads; env.de[DE_BLKSPERTRACK] = bpt;
	env.de[DE_INTERLEAVE] = 0; env.de[DE_LOWCYL] = low; env.de[DE_UPPERCYL] = upper;
	env.de[DE_MEMBUFTYPE] = 5;
	geo.dg_TotalSectors = 0x5a5a5a5a; geo.dg_Cylinders = 0x5a5a5a5a;
	nlimits = 0;
	GetDriveGeometry(&g);

	printf("  %-40s cyls=%-9u cylsec=%-6u total=%u%s\n", what, geo.dg_Cylinders, geo.dg_CylSectors,
		geo.dg_TotalSectors, geo.dg_TotalSectors == 0xFFFFFFFFu ? " (saturated)" : "");
	check(geo.dg_TotalSectors == want_total, "dg_TotalSectors");
	check(geo.dg_Cylinders == upper - low + 1, "dg_Cylinders unchanged: UpperCyl - LowCyl + 1");
	check(geo.dg_CylSectors == cs, "dg_CylSectors = heads * blocks per track");
	check(geo.dg_Heads == heads && geo.dg_TrackSectors == bpt && geo.dg_SectorSize == 512 &&
		geo.dg_BufMemType == 5 && geo.dg_DeviceType == DG_UNKNOWN && geo.dg_Flags == 0, "other fields");
	check(nlimits == 1, "partition limits recomputed once");
}

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGFPE, on_fpe);

	geometry("normal 1000 cyl, 16 heads x 63", 0, 999, 16, 63, 1008000);
	geometry("single cylinder", 7, 7, 16, 63, 1008);
	geometry("LowCyl > 0: difference is used", 10000, 70000, 16, 4096, 3932225536u);

	/* CylSectors 65536: 65535 cylinders fit, 65536 do not (product 2^32) */
	geometry("65535 cylinders x 65536 (fits)", 0, 65534, 16, 4096, 4294901760u);
	geometry("65536 cylinders x 65536 (= 2^32)", 0, 65535, 16, 4096, 0xFFFFFFFFu);
	geometry("65537 cylinders x 65536", 0, 65536, 16, 4096, 0xFFFFFFFFu);
	geometry("partition with LowCyl at the limit", 100, 100 + 65534, 16, 4096, 4294901760u);
	geometry("partition one cylinder over", 100, 100 + 65535, 16, 4096, 0xFFFFFFFFu);

	/* CylSectors 1008: 4260880 cylinders = 4294967040 sectors, one more wraps */
	geometry("4260880 cylinders x 1008 (fits)", 0, 4260879, 16, 63, 4294967040u);
	geometry("4260881 cylinders x 1008 (wraps)", 0, 4260880, 16, 63, 0xFFFFFFFFu);
	geometry("huge cylinder count x 1008", 0, 0x00FFFFFF, 16, 63, 0xFFFFFFFFu);

	/* CylSectors 1 */
	geometry("CylSectors 1, 0xFFFFFFFF cylinders", 0, 0xFFFFFFFEu, 1, 1, 0xFFFFFFFFu);
	geometry("CylSectors 1, cylinder count wraps", 0, 0xFFFFFFFFu, 1, 1, 0xFFFFFFFFu);

	/* no cylinder size: no division, plain product */
	geometry("zero heads", 0, 999, 0, 63, 0);
	geometry("zero blocks per track", 0, 999, 16, 0, 0);
	geometry("zero heads, huge cylinder count", 0, 0xFFFFFFF0u, 0, 63, 0);

	if (failures) { printf("  FAIL: %d check(s) wrong\n", failures); return 1; }
	printf("  PASS: dg_TotalSectors saturates at 2^32 sectors, CylSectors 0 is safe\n");
	return 0;
}
