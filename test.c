#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<assert.h>

typedef struct TEST_Flash_Ctrl
{         unsigned char *cmd;
        unsigned char *read_buf;
        unsigned char *w_data;
        unsigned char  cmd_len;
        unsigned char  read_buf_len;
        unsigned char  w_len;
        unsigned char  resv;
}TEST_C;

static int fd;
static int write_to_flash(TEST_C *buf)
{
        return write(fd, buf, sizeof(TEST_C));
}

static int read_from_flash(TEST_C *buf)
{
        return read(fd, buf, buf->read_buf_len);
}

static int init_flash()
{
        /*erase before write*/
        int ret ;
        TEST_C buf;
        unsigned char erase_cmd[] = {0xc7, 0x94, 0x80, 0x9a};

        buf.cmd = erase_cmd;
        buf.cmd_len = sizeof(erase_cmd);

        ret = write_to_flash(&buf);
        if (0 == ret)
        {
                printf("%s: write err.\n ", __FUNCTION__);
                return ret;
        }

        return 0;
}

static int write_buffer(unsigned short addr, unsigned char* data, unsigned char len)
{
        unsigned char w_cmd[4];
        unsigned char addr_u = (unsigned char)(addr >> 8);
        unsigned char addr_v = (unsigned char)addr;
        int ret;
        TEST_C buf;

        w_cmd[0] = 0x84;
        w_cmd[1] = 0xff;
        w_cmd[2] = addr_u;
        w_cmd[3] = addr_v;

        buf.cmd = w_cmd;
        buf.cmd_len = sizeof(w_cmd);
        buf.w_data = data;
        buf.w_len  = len;

        ret = write_to_flash(&buf);
        if (0 == ret)
        {
                printf("%s: write err.\n ", __FUNCTION__);
                return ret;
        }

        return 0;
}

static int read_buffer(unsigned short add, unsigned char *read_buf, unsigned char len)
{

        unsigned char r_cmd[5];
        unsigned char addr_u;
        unsigned char addr_v;
        TEST_C test_c;
        TEST_C buf;


        addr_u = (unsigned char)(add >> 8);
        addr_v = (unsigned char)add;

        r_cmd[0] = 0xd4;
        r_cmd[1] = 0xff;
        r_cmd[2] = addr_u;
        r_cmd[3] = addr_v;
        r_cmd[4] = 0xff;

        buf.cmd = r_cmd;
        buf.cmd_len = sizeof(r_cmd);
        buf.read_buf = read_buf;
        buf.read_buf_len = len;

        read_from_flash(&buf);
}

static unsigned char read_status()
{
        TEST_C buf;
        unsigned char cmd[1];
        unsigned char status = 0;
        printf("Get status \n");

        cmd[0] = 0xd7;

        buf.cmd = cmd;
        buf.cmd_len = 1;
        buf.read_buf = &status;
        buf.read_buf_len = 1;

        read_from_flash(&buf);

        return status;
}

int main(void)
{
        int ret = 0;
        unsigned char buf_room[10];
        unsigned char status;
        unsigned short addr = 0x0;
        unsigned char data[] = {0x00, 0xff, 0x0, 0xff, 0x0, 0xff};

        fd = open("/dev/spidev0.0", O_RDWR);
        if (fd < 0)
        {
                printf("%s open device failed. \n", __FUNCTION__);
                return -1;
        }
        init_flash();
#if 1
        write_buffer(addr, data, sizeof(data));
        read_buffer(addr, buf_room, 8);

        printf("Data is %x \n", buf_room[0]);
        printf("Data is %x \n", buf_room[1]);
        printf("Data is %x \n", buf_room[2]);
#endif
        ret = close(fd);

        return 0;
}
