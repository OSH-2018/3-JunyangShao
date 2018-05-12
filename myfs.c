#define FUSE_USE_VERSION 26
#define blocknr 16384
#define maxblock  16000
//#define blocksize 32768
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
int blocksize = 32768;	//一页32kb
struct filenode                         //文件节点存储为链表
{
    char filename[64];		//文件名
	int file_block;		//文件结点使用的页号
	int block_num;		//文件总共拥有页数
	unsigned short content[maxblock];		//content记录页号
	struct stat st;		//st是文件信息
    struct filenode *next;		//采用链表储存，头插法
};
static void *mem[ blocknr ];             
static struct filenode *root = NULL;
//用于链表操作的一个值，不在内存映射中
static struct filenode **root_for_begin;
//用来存储指向链表头的指针，一定可以在mem[0]找到它(在init中完成设置)
int mstack[blocknr];
int mstack_pointer = blocknr - 1;
int get_mem() {
	int blocknum;

	if (mstack_pointer > 0) {

		blocknum = mstack[mstack_pointer];

		mem[blocknum] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		memset(mem[blocknum], 0, blocksize);

		mstack_pointer--;

		return blocknum;

	}
	else {
		return -1;//若栈内已无可取元素，返回错误信号
	}
}	//取得内存

void free_mem(int blocknum) {

	munmap(mem[blocknum], blocksize);

	mem[blocknum] = NULL;

	mstack_pointer++;

	mstack[mstack_pointer] = blocknum;//被释放的元素推入栈

	return;

}	//释放内存

int content_change(struct filenode *fnode,int size) {
	int i = 0, j = 0;
	int msize = -size;
	int curr_page_num = fnode->block_num;
	int temp;
	int mem_got[blocknr];
	if (size < 0) {
		for (i = 0; i < msize; i++) {
			free_mem(fnode->content[curr_page_num - i - 1 ]);
		}
		fnode->block_num -= msize;	//改变block数的记录
		return -i;
	}	//当要释放内存时
	else {
		for (i = 0; i < size && (fnode->block_num + i)<maxblock; i++) {
			temp = get_mem();
			if (temp == -1)break;
			else {
				mem_got[i] = temp;
			}
		}	//当要扩充内存时，不能扩充超过上限
		for (j = 0; j < i; j++) {
			fnode->content[curr_page_num + j] = mem_got[j];
		}
		fnode->block_num += i;	//改变block数的记录
		return i;	//返回成功取得的block数
	}
}		

static struct filenode *get_filenode(const char *name)          
//寻找文件结点
{
    struct filenode *node = root;
    while(node)
    {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)       
//创造文件结点（空结点）
{
	int memnum;
	memnum = get_mem();
	struct filenode *new = (struct filenode *)mem[memnum];
    memcpy(new->filename, filename, strlen(filename) + 1);
    memcpy(&new->st, st, sizeof(struct stat));
    new->next = root;
	new->block_num = 0;
	new->file_block = memnum;
	root = new;
	*root_for_begin = root;
	//printf("root for begin %d\nroot %d\nroot_fbc %d\n ", root_for_begin, root,*root_for_begin);
}

static void *oshfs_init(struct fuse_conn_info *conn)
//初始化mem栈
{
	int i = 0;
	for (i = 0; i < blocknr; i++) {
		mem[i] = NULL;
		mstack[i] = blocknr - i - 1;
	}
	mstack_pointer = blocknr - 1;
	int z;
	z = get_mem();
	root_for_begin = (struct filenode**)mem[z];//将root_for_begin映射到mem[0]中去;
	*root_for_begin = NULL;//初始设定文件系统为空
	//printf("root for begin %d\nroot %d\nz %d\n ", root_for_begin, root,z);
	return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
//寻找文件信息
{
    int ret = 0;
    struct filenode *node = get_filenode(path);        
    if(strcmp(path, "/") == 0)                          
    {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;             
    }
    else if(node)                                     
        memcpy(stbuf, &node->st, sizeof(struct stat));
    else                                             
        ret = -ENOENT;
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
//读出文件信息(所有)
{
    struct filenode *node = root;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node)
    {                                  
        filler(buf, node->filename, &node->st, 0);
        node = node->next;
    }	//依次遍历链表
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)           
{
    struct stat st;                                                        
    st.st_mode = S_IFREG | 0644;                                          
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;                                                        
    st.st_size = 0;                                                        
    create_filenode(path + 1, &st);                                        
    return 0;
}//创造结点（更高层次，调用create_filenode）

static int oshfs_open(const char *path, struct fuse_file_info *fi)          
//由于文件系统已经在内存中运行，所以open不必实现了
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//将缓冲区(buf)中的数据写到一个打开的文件中
{
    struct filenode *node = get_filenode(path);                     //打开文件
	int s;
	int c = (offset + size - 1) / blocksize + 1 - (node->block_num);//要增（减）的页数
	if (c < 0)c = 0;//写是不会截短的
	s=content_change(node, c);//获取成功增（减）的页数（以及增（减）页）
	if (s == c) {//得到正确结果
		node->st.st_size = offset + size;
	}                 
	else //错误处理
	{
		return -1;
	}
	int f = offset / blocksize;	//计算被写入的第一页的页码
	int o = offset % blocksize ;//计算从第一页哪里开始写入
	int n = (o + size - 1) / blocksize;
	int i = 0;
	char *buf0 = buf;
	if (n == 0)
	{
		memcpy(mem[node->content[f]] + o, buf0, size);
	}
	else 
	{
		for (i = 0; i <= n; i++) {
			if (i == 0) {
				memcpy(mem[node->content[f]] + o, buf0, blocksize - o );
				buf0 += blocksize - o;
			}
			else if (i <= n - 1) {
				memcpy(mem[node->content[f + i]], buf0, blocksize);
				buf0 += blocksize;
			}
			else {
				memcpy(mem[node->content[f + i]], buf0, size - (long int)(buf0-buf));
			}
		}
	}
    return size;
}

static int oshfs_truncate(const char *path, off_t size)    
{
    struct filenode *node = get_filenode(path);          
	int c = size / blocksize - node->block_num;//c为增（减）页数
	int s;//s为成功增（减）的页数
	s = content_change(node, c);
	if (s == c) {//得到正确结果
		node->st.st_size = size;
	}
	else {//错误处理
		return -1;
	}
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//从一个已经打开的文件中读出数据
{
    struct filenode *node = get_filenode(path);                     //寻找对应的节点
    long int ret=size;//记录读取量
	if (offset + size > node->st.st_size) {//超量
		ret = node->st.st_size - offset;
	}
	if (ret < 0)return ret;//读错了
	int f = offset / blocksize;		//f=读起始页
	int o = offset % blocksize;		//o=页内偏移
	int n = (o + ret - 1) / blocksize;//n=被读取的页数-1
	int i = 0;
	char* buf0 = buf;
	if (n == 0) {//不需要跨页读取
		memcpy(buf, mem[node->content[f]] + o, ret);
	}
	else {//需要跨页读取
		for (i = 0; i <= n; i++) {
			if (i == 0) 
			{
				memcpy(buf0, mem[node->content[f]] + o, blocksize - o);
				buf0 += blocksize - o;
			}
			else if (i <= n - 1)
			{
				memcpy(buf0, mem[node->content[f + i]],  blocksize);
				buf0 += blocksize;
			}
			else 
			{
				memcpy(buf0, mem[node->content[f + i]],ret - (long int)(buf0 - buf));
			}
		}
	}
	return ret;
}

static int oshfs_unlink(const char *path)               //用于删除一个节点
{
    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root;
    if (node1==root)                        //特殊处理文件为链表头的情况
    {
        root=node1->next;
		*root_for_begin = root;
		//printf("root for begin %d\nroot %d\nroot_fbc %d\n ", root_for_begin, root, *root_for_begin);
        node1->next=NULL;
    }
    else if (node1)                         //若node1存在
    {
            while(node2->next!=node1&&node2!=NULL)
                    node2 = node2->next;
            node2->next=node1->next;
            node1->next=NULL;
			//printf("root for begin %d\nroot %d\nroot_fbc %d\n ", root_for_begin, root, *root_for_begin);
    }
	int i = 0;
	for (i = 0; i < node1->block_num; i++)free_mem(node1->content[i]);
	free_mem(node1->file_block);
    return 0;
    //开始删除node1节点
}

static const struct fuse_operations op = {              //不同的op所对应的函数
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);            //调用fuse函数
}
