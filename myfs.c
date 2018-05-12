#define FUSE_USE_VERSION 26
#define blocknr 16384
#define maxblock  16000
//#define blocksize 32768
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
int blocksize = 32768;	//һҳ32kb
struct filenode                         //�ļ��ڵ�洢Ϊ����
{
    char filename[64];		//�ļ���
	int file_block;		//�ļ����ʹ�õ�ҳ��
	int block_num;		//�ļ��ܹ�ӵ��ҳ��
	unsigned short content[maxblock];		//content��¼ҳ��
	struct stat st;		//st���ļ���Ϣ
    struct filenode *next;		//���������棬ͷ�巨
};
static void *mem[ blocknr ];             
static struct filenode *root = NULL;
//�������������һ��ֵ�������ڴ�ӳ����
static struct filenode **root_for_begin;
//�����洢ָ������ͷ��ָ�룬һ��������mem[0]�ҵ���(��init���������)
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
		return -1;//��ջ�����޿�ȡԪ�أ����ش����ź�
	}
}	//ȡ���ڴ�

void free_mem(int blocknum) {

	munmap(mem[blocknum], blocksize);

	mem[blocknum] = NULL;

	mstack_pointer++;

	mstack[mstack_pointer] = blocknum;//���ͷŵ�Ԫ������ջ

	return;

}	//�ͷ��ڴ�

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
		fnode->block_num -= msize;	//�ı�block���ļ�¼
		return -i;
	}	//��Ҫ�ͷ��ڴ�ʱ
	else {
		for (i = 0; i < size && (fnode->block_num + i)<maxblock; i++) {
			temp = get_mem();
			if (temp == -1)break;
			else {
				mem_got[i] = temp;
			}
		}	//��Ҫ�����ڴ�ʱ���������䳬������
		for (j = 0; j < i; j++) {
			fnode->content[curr_page_num + j] = mem_got[j];
		}
		fnode->block_num += i;	//�ı�block���ļ�¼
		return i;	//���سɹ�ȡ�õ�block��
	}
}		

static struct filenode *get_filenode(const char *name)          
//Ѱ���ļ����
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
//�����ļ���㣨�ս�㣩
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
//��ʼ��memջ
{
	int i = 0;
	for (i = 0; i < blocknr; i++) {
		mem[i] = NULL;
		mstack[i] = blocknr - i - 1;
	}
	mstack_pointer = blocknr - 1;
	int z;
	z = get_mem();
	root_for_begin = (struct filenode**)mem[z];//��root_for_beginӳ�䵽mem[0]��ȥ;
	*root_for_begin = NULL;//��ʼ�趨�ļ�ϵͳΪ��
	//printf("root for begin %d\nroot %d\nz %d\n ", root_for_begin, root,z);
	return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
//Ѱ���ļ���Ϣ
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
//�����ļ���Ϣ(����)
{
    struct filenode *node = root;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node)
    {                                  
        filler(buf, node->filename, &node->st, 0);
        node = node->next;
    }	//���α�������
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
}//�����㣨���߲�Σ�����create_filenode��

static int oshfs_open(const char *path, struct fuse_file_info *fi)          
//�����ļ�ϵͳ�Ѿ����ڴ������У�����open����ʵ����
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//��������(buf)�е�����д��һ���򿪵��ļ���
{
    struct filenode *node = get_filenode(path);                     //���ļ�
	int s;
	int c = (offset + size - 1) / blocksize + 1 - (node->block_num);//Ҫ����������ҳ��
	if (c < 0)c = 0;//д�ǲ���ض̵�
	s=content_change(node, c);//��ȡ�ɹ�����������ҳ�����Լ���������ҳ��
	if (s == c) {//�õ���ȷ���
		node->st.st_size = offset + size;
	}                 
	else //������
	{
		return -1;
	}
	int f = offset / blocksize;	//���㱻д��ĵ�һҳ��ҳ��
	int o = offset % blocksize ;//����ӵ�һҳ���￪ʼд��
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
	int c = size / blocksize - node->block_num;//cΪ��������ҳ��
	int s;//sΪ�ɹ�����������ҳ��
	s = content_change(node, c);
	if (s == c) {//�õ���ȷ���
		node->st.st_size = size;
	}
	else {//������
		return -1;
	}
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//��һ���Ѿ��򿪵��ļ��ж�������
{
    struct filenode *node = get_filenode(path);                     //Ѱ�Ҷ�Ӧ�Ľڵ�
    long int ret=size;//��¼��ȡ��
	if (offset + size > node->st.st_size) {//����
		ret = node->st.st_size - offset;
	}
	if (ret < 0)return ret;//������
	int f = offset / blocksize;		//f=����ʼҳ
	int o = offset % blocksize;		//o=ҳ��ƫ��
	int n = (o + ret - 1) / blocksize;//n=����ȡ��ҳ��-1
	int i = 0;
	char* buf0 = buf;
	if (n == 0) {//����Ҫ��ҳ��ȡ
		memcpy(buf, mem[node->content[f]] + o, ret);
	}
	else {//��Ҫ��ҳ��ȡ
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

static int oshfs_unlink(const char *path)               //����ɾ��һ���ڵ�
{
    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root;
    if (node1==root)                        //���⴦���ļ�Ϊ����ͷ�����
    {
        root=node1->next;
		*root_for_begin = root;
		//printf("root for begin %d\nroot %d\nroot_fbc %d\n ", root_for_begin, root, *root_for_begin);
        node1->next=NULL;
    }
    else if (node1)                         //��node1����
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
    //��ʼɾ��node1�ڵ�
}

static const struct fuse_operations op = {              //��ͬ��op����Ӧ�ĺ���
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
    return fuse_main(argc, argv, &op, NULL);            //����fuse����
}
