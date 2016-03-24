/*
 * This file is part of libzbc.
 *
 * Copyright (C) 2009-2014, HGST, Inc.  All rights reserved.
 *
 * This software is distributed under the terms of the BSD 2-clause license,
 * "as is," without technical support, and WITHOUT ANY WARRANTY, without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. You should have received a copy of the BSD 2-clause license along
 * with libzbc. If not, see  <http://opensource.org/licenses/BSD-2-Clause>.
 *
 * Author: Damien Le Moal (damien.lemoal@hgst.com)
 *         Christophe Louargant (christophe.louargant@hgst.com)
 */

/***** Including files *****/

#include "zbc.h"
#include "zbc_sg.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

/***** Private data *****/

/**
 * Definition of the commands
 * Each command is defined by 3 fields.
 * It's name, it's length, and it's opcode.
 */
static struct zbc_sg_cmd_s
{

    char                *cdb_cmd_name;
    int                 cdb_opcode;
    int                 cdb_sa;
    size_t              cdb_length;
    int			dir;

} zbc_sg_cmd_list[ZBC_SG_CMD_NUM] = {

    /* ZBC_SG_TEST_UNIT_READY */
    {
        "TEST UNIT READY",
        ZBC_SG_TEST_UNIT_READY_CDB_OPCODE,
        0,
        ZBC_SG_TEST_UNIT_READY_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_INQUIRY */
    {
        "INQUIRY",
        ZBC_SG_INQUIRY_CDB_OPCODE,
        0,
        ZBC_SG_INQUIRY_CDB_LENGTH,
	SG_DXFER_FROM_DEV
    },

    /* ZBC_SG_READ_CAPACITY */
    {
        "READ CAPACITY 16",
        ZBC_SG_READ_CAPACITY_CDB_OPCODE,
        ZBC_SG_READ_CAPACITY_CDB_SA,
        ZBC_SG_READ_CAPACITY_CDB_LENGTH,
	SG_DXFER_FROM_DEV
    },

    /* ZBC_SG_READ */
    {
        "READ 16",
        ZBC_SG_READ_CDB_OPCODE,
        0,
        ZBC_SG_READ_CDB_LENGTH,
	SG_DXFER_FROM_DEV
    },

    /* ZBC_SG_WRITE */
    {
        "WRITE 16",
        ZBC_SG_WRITE_CDB_OPCODE,
        0,
        ZBC_SG_WRITE_CDB_LENGTH,
	SG_DXFER_TO_DEV
    },

    /* ZBC_SG_SYNC_CACHE */
    {
        "SYNCHRONIZE CACHE 16",
        ZBC_SG_SYNC_CACHE_CDB_OPCODE,
        0,
        ZBC_SG_SYNC_CACHE_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_REPORT_ZONES */
    {
        "REPORT ZONES",
        ZBC_SG_REPORT_ZONES_CDB_OPCODE,
        ZBC_SG_REPORT_ZONES_CDB_SA,
        ZBC_SG_REPORT_ZONES_CDB_LENGTH,
	SG_DXFER_FROM_DEV
    },

    /* ZBC_SG_OPEN_ZONE */
    {
        "OPEN ZONE",
        ZBC_SG_OPEN_ZONE_CDB_OPCODE,
        ZBC_SG_OPEN_ZONE_CDB_SA,
        ZBC_SG_OPEN_ZONE_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_CLOSE_ZONE */
    {
        "CLOSE ZONE",
        ZBC_SG_CLOSE_ZONE_CDB_OPCODE,
        ZBC_SG_CLOSE_ZONE_CDB_SA,
        ZBC_SG_CLOSE_ZONE_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_FINISH_ZONE */
    {
        "FINISH ZONE",
        ZBC_SG_FINISH_ZONE_CDB_OPCODE,
        ZBC_SG_FINISH_ZONE_CDB_SA,
        ZBC_SG_FINISH_ZONE_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_RESET_WRITE_POINTER */
    {
        "RESET WRITE POINTER",
        ZBC_SG_RESET_WRITE_POINTER_CDB_OPCODE,
        ZBC_SG_RESET_WRITE_POINTER_CDB_SA,
        ZBC_SG_RESET_WRITE_POINTER_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_SET_ZONES */
    {
        "SET ZONES",
        ZBC_SG_SET_ZONES_CDB_OPCODE,
        ZBC_SG_SET_ZONES_CDB_SA,
        ZBC_SG_SET_ZONES_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_SET_WRITE_POINTER */
    {
        "SET WRITE POINTER",
        ZBC_SG_SET_WRITE_POINTER_CDB_OPCODE,
        ZBC_SG_SET_WRITE_POINTER_CDB_SA,
        ZBC_SG_SET_WRITE_POINTER_CDB_LENGTH,
	SG_DXFER_NONE
    },

    /* ZBC_SG_ATA12 */
    {
	"ATA 12",
	ZBC_SG_ATA12_CDB_OPCODE,
	0,
        ZBC_SG_ATA12_CDB_LENGTH,
	0
    },

    /* ZBC_SG_ATA16 */
    {
	"ATA 16",
	ZBC_SG_ATA16_CDB_OPCODE,
	0,
        ZBC_SG_ATA16_CDB_LENGTH,
	0
    }

};

/**
 * Get a command name from its operation code in a CDB.
 */
static char *
zbc_sg_cmd_name(zbc_sg_cmd_t *cmd)
{
    char *name;

    if ( (cmd->code >= 0)
         && (cmd->code < ZBC_SG_CMD_NUM) ) {
        name = zbc_sg_cmd_list[cmd->code].cdb_cmd_name;
    } else {
        name = "(UNKNOWN COMMAND)";
    }

    return( name );

}

/**
 * Set ASC, ASCQ.
 */
static void
zbc_sg_set_sense(zbc_device_t *dev,
                 uint8_t *sense_buf)
{

    if ( sense_buf == NULL ) {

        dev->zbd_errno.sk       = 0x00;
        dev->zbd_errno.asc_ascq = 0x0000;

    } else {

        if ( (sense_buf[0] & 0x7F) == 0x72
             || (sense_buf[0] & 0x7F) == 0x73 ) {

            /* store sense key, ASC/ASCQ */
            dev->zbd_errno.sk       = sense_buf[1] & 0x0F;
            dev->zbd_errno.asc_ascq = ((int)sense_buf[2] << 8) | (int)sense_buf[3];

        } else if ( (sense_buf[0] & 0x7F) == 0x70
                    || (sense_buf[0] & 0x7F) == 0x71 ) {

            /* store sense key, ASC/ASCQ */
            dev->zbd_errno.sk       = sense_buf[2] & 0x0F;
            dev->zbd_errno.asc_ascq = ((int)sense_buf[12] << 8) | (int)sense_buf[13];

        }
    }

    return;

}

/**
 * Free a command.
 */
void
zbc_sg_cmd_destroy(zbc_sg_cmd_t *cmd)
{

    /* Free the command */
    if ( cmd ) {
        if ( cmd->out_buf
             && cmd->out_buf_needfree ) {
            free(cmd->out_buf);
            cmd->out_buf = NULL;
            cmd->out_bufsz = 0;
        }
    }

    return;

}

/**
 * Allocate and initialize a new command.
 */
int
zbc_sg_cmd_init(zbc_sg_cmd_t *cmd,
                int cmd_code,
                uint8_t *out_buf,
                size_t out_bufsz)
{
    int ret = 0;

    if ( (! cmd)
         || (cmd_code < 0)
         || (cmd_code >= ZBC_SG_CMD_NUM) ) {
        zbc_error("Invalid command specified\n");
        return( -EINVAL );
    }

    /* Set command */
    memset(cmd, 0, sizeof(zbc_sg_cmd_t));
    cmd->code = cmd_code;
    cmd->cdb_sz = zbc_sg_cmd_list[cmd_code].cdb_length;
    zbc_assert(cmd->cdb_sz <= ZBC_SG_CDB_MAX_LENGTH);
    cmd->cdb_opcode = zbc_sg_cmd_list[cmd_code].cdb_opcode;
    cmd->cdb_sa = zbc_sg_cmd_list[cmd_code].cdb_sa;

    /* Set output buffer */
    if ( out_buf ) {

        /* Set specified buffer */
        if ( ! out_bufsz ) {
            zbc_error("Invalid 0 output buffer size\n");
            ret = -EINVAL;
            goto out;
        }

        cmd->out_buf = out_buf;
        cmd->out_bufsz = out_bufsz;

    } else if ( out_bufsz ) {

        /* Allocate a buffer */
        ret = posix_memalign((void **) &cmd->out_buf, sysconf(_SC_PAGESIZE), out_bufsz);
        if ( ret != 0 ) {
            zbc_error("No memory for output buffer (%zu B)\n",
                      out_bufsz);
            ret = -ENOMEM;
            goto out;
        }
        memset(cmd->out_buf, 0, out_bufsz);
        cmd->out_bufsz = out_bufsz;
        cmd->out_buf_needfree = 1;

    }

    /* OK: setup SGIO header */
    memset(&cmd->io_hdr, 0, sizeof(sg_io_hdr_t));

    cmd->io_hdr.interface_id    = 'S';
    cmd->io_hdr.timeout         = 20000;
    cmd->io_hdr.flags           = 0; //SG_FLAG_DIRECT_IO;

    cmd->io_hdr.cmd_len         = cmd->cdb_sz;
    cmd->io_hdr.cmdp            = &cmd->cdb[0];

    cmd->io_hdr.dxfer_direction = zbc_sg_cmd_list[cmd_code].dir;
    cmd->io_hdr.dxfer_len       = cmd->out_bufsz;
    cmd->io_hdr.dxferp          = cmd->out_buf;

    cmd->io_hdr.mx_sb_len       = ZBC_SG_SENSE_MAX_LENGTH;
    cmd->io_hdr.sbp             = cmd->sense_buf;

out:

    if ( ret != 0 ) {
        zbc_sg_cmd_destroy(cmd);
    }

    return( ret );

}

/**
 * Execute a command.
 */
int
zbc_sg_cmd_exec(zbc_device_t *dev,
                zbc_sg_cmd_t *cmd)
{
    int ret;

    if ( zbc_log_level >= ZBC_LOG_DEBUG ) {
        zbc_debug("%s: Sending command 0x%02x:0x%02x (%s):\n",
                  dev->zbd_filename,
                  cmd->cdb_opcode,
                  cmd->cdb_sa,
                  zbc_sg_cmd_name(cmd));
	zbc_sg_print_bytes(dev, cmd->cdb, cmd->cdb_sz);
    }

    /* Send the SG_IO command */
    ret = ioctl(dev->zbd_fd, SG_IO, &cmd->io_hdr);
    if ( ret != 0 ) {
        zbc_error("%s: SG_IO ioctl failed %d (%s)\n",
                  dev->zbd_filename,
                  errno,
                  strerror(errno));
        ret = -errno;
        goto out;
    }

    /* Reset errno */
    zbc_sg_set_sense(dev, NULL);

    zbc_debug("%s: Command %s done: status 0x%02x (0x%02x), host status 0x%04x, driver status 0x%04x (flags 0x%04x)\n",
              dev->zbd_filename,
              zbc_sg_cmd_name(cmd),
              (unsigned int)cmd->io_hdr.status,
              (unsigned int)cmd->io_hdr.masked_status,
              (unsigned int)cmd->io_hdr.host_status,
              (unsigned int)zbc_sg_cmd_driver_status(cmd),
              (unsigned int)zbc_sg_cmd_driver_flags(cmd));

    /* Check status */
    if ( ((cmd->code == ZBC_SG_ATA12) || (cmd->code == ZBC_SG_ATA16))
         && (cmd->cdb[2] & (1 << 5)) ) {

       /* ATA command status */
       if ( cmd->io_hdr.status != ZBC_SG_CHECK_CONDITION ) {
           zbc_sg_set_sense(dev, cmd->sense_buf);
           ret = -EIO;
           goto out;
       }

       if ( (zbc_sg_cmd_driver_status(cmd) == ZBC_SG_DRIVER_SENSE)
            && (cmd->io_hdr.sb_len_wr > 21)
            && (cmd->sense_buf[21] != 0x50) ) {
           zbc_sg_set_sense(dev, cmd->sense_buf);
           ret = -EIO;
           goto out;
       }

       cmd->io_hdr.status = 0;

    }

    if ( cmd->io_hdr.status
         || (cmd->io_hdr.host_status != ZBC_SG_DID_OK)
	 || (zbc_sg_cmd_driver_status(cmd) && (zbc_sg_cmd_driver_status(cmd) != ZBC_SG_DRIVER_SENSE)) ) {

	if ( zbc_log_level >= ZBC_LOG_DEBUG ) {

	    zbc_error("%s: Command %s failed with status 0x%02x (0x%02x), host status 0x%04x, driver status 0x%04x (flags 0x%04x)\n",
		      dev->zbd_filename,
		      zbc_sg_cmd_name(cmd),
		      (unsigned int)cmd->io_hdr.status,
		      (unsigned int)cmd->io_hdr.masked_status,
		      (unsigned int)cmd->io_hdr.host_status,
		      (unsigned int)zbc_sg_cmd_driver_status(cmd),
		      (unsigned int)zbc_sg_cmd_driver_flags(cmd));

            if ( cmd->io_hdr.sb_len_wr ) {
                zbc_debug("%s: Sense data (%d B):\n",
			  dev->zbd_filename,
			  cmd->io_hdr.sb_len_wr);
		zbc_sg_print_bytes(dev, cmd->sense_buf, cmd->io_hdr.sb_len_wr);
            } else {
                zbc_debug("%s: No sense data\n", dev->zbd_filename);
            }

	}

        zbc_sg_set_sense(dev, cmd->sense_buf);
        ret = -EIO;

        goto out;

    }

    if ( cmd->io_hdr.resid ) {
        zbc_debug("%s: Transfer missing %d B of data\n",
                  dev->zbd_filename,
                  cmd->io_hdr.resid);
        cmd->out_bufsz -= cmd->io_hdr.resid;
    }

    zbc_debug("%s: Command %s executed in %u ms, %zu B transfered\n",
              dev->zbd_filename,
              zbc_sg_cmd_name(cmd),
              cmd->io_hdr.duration,
              cmd->out_bufsz);

out:

    return( ret );

}

/**
 * Test if unit is ready. This will retry 5 times if the command
 * returns "UNIT ATTENTION".
 */
int
zbc_sg_cmd_test_unit_ready(zbc_device_t *dev)
{
    zbc_sg_cmd_t cmd;
    int ret = -EAGAIN, retries = 5;

    while( retries && (ret == -EAGAIN) ) {

	retries--;

	/* Intialize command */
	ret = zbc_sg_cmd_init(&cmd, ZBC_SG_TEST_UNIT_READY, NULL, 0);
	if ( ret != 0 ) {
	    zbc_error("%s: zbc_sg_cmd_init TEST UNIT READY failed\n",
		      dev->zbd_filename);
	    return( ret );
	}
	cmd.cdb[0] = ZBC_SG_TEST_UNIT_READY_CDB_OPCODE;

	/* Execute the SG_IO command */
	ret = zbc_sg_cmd_exec(dev, &cmd);
	if ( (ret != 0)
	     && ((cmd.io_hdr.host_status == ZBC_SG_DID_SOFT_ERROR)
		 || (cmd.io_hdr.sb_len_wr && (cmd.sense_buf[2] == 0x06))) ) {
	    zbc_debug("%s: Unit attention required, %d / 5 retries left\n",
		      dev->zbd_filename,
		      retries);
	    ret = -EAGAIN;
	}

	zbc_sg_cmd_destroy(&cmd);

    }

    if ( ret != 0 ) {
	ret = -ENXIO;
    }

    return( ret );

}

/**
 * Fill the buffer with the result of INQUIRY command.
 * buf must be at least ZBC_SG_INQUIRY_REPLY_LEN bytes long.
 */
int
zbc_sg_cmd_inquiry(zbc_device_t *dev,
                   void *buf)
{
    zbc_sg_cmd_t cmd;
    int ret;

    /* Allocate and intialize inquiry command */
    ret = zbc_sg_cmd_init(&cmd, ZBC_SG_INQUIRY, NULL, ZBC_SG_INQUIRY_REPLY_LEN);
    if ( ret != 0 ) {
        zbc_error("%s: zbc_sg_cmd_init INQUIRY failed\n",
		  dev->zbd_filename);
        return( ret );
    }

    /* Fill command CDB:
     * +=============================================================================+
     * |  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
     * |Byte |        |        |        |        |        |        |        |        |
     * |=====+=======================================================================|
     * | 0   |                           Operation Code (12h)                        |
     * |-----+-----------------------------------------------------------------------|
     * | 1   | Logical Unit Number      |                  Reserved         |  EVPD  |
     * |-----+-----------------------------------------------------------------------|
     * | 2   |                           Page Code                                   |
     * |-----+-----------------------------------------------------------------------|
     * | 3   | (MSB)                                                                 |
     * |- - -+---                    Allocation Length                            ---|
     * | 4   |                                                                 (LSB) |
     * |-----+-----------------------------------------------------------------------|
     * | 5   |                           Control                                     |
     * +=============================================================================+
     */
    cmd.cdb[0] = ZBC_SG_INQUIRY_CDB_OPCODE;
    zbc_sg_cmd_set_int16(&cmd.cdb[3], ZBC_SG_INQUIRY_REPLY_LEN);

    /* Execute the SG_IO command */
    ret = zbc_sg_cmd_exec(dev, &cmd);
    if ( ret == 0 ) {

        memcpy(buf, cmd.out_buf, ZBC_SG_INQUIRY_REPLY_LEN);

    }

    zbc_sg_cmd_destroy(&cmd);

    return( ret );

}

/**
 * Set bytes in a command cdb.
 */
void
zbc_sg_cmd_set_bytes(uint8_t *cmd,
                     void *buf,
                     int bytes)
{
    uint8_t *v = (uint8_t *) buf;
    int i;

    for(i = 0; i < bytes; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        /* The least significant byte is stored last */
        cmd[bytes - i - 1] = v[i];
#else
        /* The most significant byte is stored first */
        cmd[i] = v[i];
#endif
    }

    return;

}

/**
 * Get bytes from a command output buffer.
 */
void
zbc_sg_cmd_get_bytes(uint8_t *val,
                     union converter *conv,
                     int bytes)
{
    uint8_t *v = (uint8_t *) val;
    int i;

    memset(conv, 0, sizeof(union converter));

    for(i = 0; i < bytes; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        conv->val_buf[bytes - i - 1] = v[i];
#else
        conv->val_buf[i] = v[i];
#endif
    }

    return;

}

/**
 * Print an array of bytes.
 */
void
zbc_sg_print_bytes(zbc_device_t *dev,
		   uint8_t *buf,
		   unsigned int len)
{
    char msg[512];
    unsigned i = 0, j;
    int n;

    zbc_debug("%s: * +==================================\n", dev->zbd_filename);
    zbc_debug("%s: * |Byte |   0  |  1   |  2   |  3   |\n", dev->zbd_filename);
    zbc_debug("%s: * |=====+======+======+======+======+\n", dev->zbd_filename);

    while( i < len ) {

	n = sprintf(msg, "%s: * | %3d |", dev->zbd_filename, i);
	for(j = 0; j < 4; j++) {
	    if ( i < len ) {
		n += sprintf(msg + n, " 0x%02x |",
			     (unsigned int)buf[i]);
	    } else {
		n += sprintf(msg + n, "      |");
	    }
	    i++;
	}

	zbc_debug("%s\n", msg);
	if ( i < (len - 4) ) {
	    zbc_debug("%s: * |=====+======+======+======+======+\n", dev->zbd_filename);
	} else {
	    zbc_debug("%s: * +==================================\n", dev->zbd_filename);
	}
    }

    return;

}
