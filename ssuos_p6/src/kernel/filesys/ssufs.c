#include <filesys/ssufs.h>
#include <filesys/vnode.h>
#include <device/block.h>
#include <device/ata.h>
#include <proc/proc.h>
#include <ssulib.h>
#include <string.h>
#include <bitmap.h>

#define MIN(a, b)		(a<b?a:b)

static unsigned char bitmapblock[SSU_BLOCK_SIZE];
static struct ssufs_superblock ssufs_sb;
extern struct blk_dev ata1_blk_dev;
extern struct process *cur_process;

char tmpblock[SSU_BLOCK_SIZE];

struct vnode *init_ssufs(char *volname, uint32_t lba, struct vnode *mnt_root){
	int result;
	int i;
	char superblock[SSU_BLOCK_SIZE];

	ssufs_sb.blkdev = &ata1_blk_dev;

	result = ssufs_readblock(&ssufs_sb, lba, superblock);

	memcpy(&ssufs_sb, superblock, sizeof(ssufs_sb));
	ssufs_sb.blkdev = &ata1_blk_dev;

	if(ssufs_sb.sb_magic != SSU_SB_MAGIC){
		ssufs_sb.sb_nblocks = (ssufs_sb.blkdev->blk_count / (SSU_BLOCK_SIZE / ssufs_sb.blkdev->blk_size));
		ssufs_sb.lba = lba;
		// sb.sb->ssufs_info->sb_nblocks;
	}else{

	}

	//load or init bitmap block
	ssufs_load_databitmapblock(&ssufs_sb);
	ssufs_load_inodebitmapblock(&ssufs_sb);
	ssufs_sync(&ssufs_sb);

	//laod or init inode table
	ssufs_load_inodetable(&ssufs_sb);

	return make_vnode_tree(&ssufs_sb, mnt_root);
}

int ssufs_load_inodetable(struct ssufs_superblock *sb)
{
	int result = 0;
	int i;
	struct ssufs_inode *root_inode;
	struct dirent dirent;

	for(i=0; i<NUM_INODE_BLOCK; i++)
		ssufs_readblock(sb, SSU_INODE_BLOCK(sb->lba) + i, ((char*)ssufs_inode_table) + (i * SSU_BLOCK_SIZE));	

	//no root directory
	if(!bitmap_test(sb->inodemap, INODE_ROOT)){
		memset(ssufs_inode_table, 0x00, sizeof(struct ssufs_inode) * NUM_INODE);

		//Unvalid, Reserved
		bitmap_set(sb->inodemap, 0, true);
		bitmap_set(sb->inodemap, 1, true);

		bitmap_set(sb->inodemap, INODE_ROOT, true);

		root_inode = &ssufs_inode_table[INODE_ROOT];
		root_inode->i_no = INODE_ROOT;
		root_inode->i_size = 0;
		root_inode->i_type = SSU_DIR_TYPE;
		root_inode->i_refcount = 1;
		root_inode->ssufs_sb = sb;

		//set root dirent
		dirent.d_ino = INODE_ROOT;
		dirent.d_type = SSU_DIR_TYPE;
		memcpy(dirent.d_name, ".", sizeof("."));
		ssufs_inode_write(root_inode, 0, (char *)&dirent, sizeof(struct dirent));
		memcpy(dirent.d_name, "..", sizeof(".."));
		ssufs_inode_write(root_inode,root_inode->i_size, (char *)&dirent, sizeof(struct dirent));

		ssufs_sync_bitmapblock(sb);
		ssufs_sync_inodetable(sb);
		printk("initialize inodetable : %d\n", NUM_INODE);
	}else{
		printk("load inodetable : %d\n", NUM_INODE);
	}

	return result;	
}

int ssufs_load_databitmapblock(struct ssufs_superblock *sb)
{
	int result;
	int i;

	//load bitmap block from HDD
	result = ssufs_readblock(sb, SSU_BITMAP_BLOCK(sb->lba), bitmapblock);

	//load block bitmap
	sb->blkmap = (struct bitmap*)bitmapblock;
	if(sb->blkmap->bit_cnt != sb->sb_nblocks){
		sb->blkmap = bitmap_create_in_buf(ssufs_sb.sb_nblocks, bitmapblock, SSU_BLOCK_SIZE/2);
		for(i=0; i<SSU_BITMAP_BLOCK(sb->lba); i++)
		{
			bitmap_set(sb->blkmap, i, true);
		}

		printk("initialize block bitmap : %d bit\n", sb->blkmap->bit_cnt);
	}else{
		printk("load block bitmap : %d bit\n", sb->blkmap->bit_cnt);
	}

	return result;
}

int ssufs_load_inodebitmapblock(struct ssufs_superblock *sb)
{
	//load inode bitmap
	sb->inodemap = (struct bitmap*)(bitmapblock + SSU_BLOCK_SIZE/2);
	if(sb->inodemap->bit_cnt != NUM_INODE)
	{
		sb->inodemap = bitmap_create_in_buf(NUM_INODE, bitmapblock + SSU_BLOCK_SIZE/2, SSU_BLOCK_SIZE/2);
		printk("initialize inode bitmap : %d bit\n", sb->inodemap->bit_cnt);
	}
	else
		printk("load inode bitmap : %d bit\n", sb->inodemap->bit_cnt);
}

int ssufs_readblock(struct ssufs_superblock *sb, uint32_t blknum, char *buf)
{ 
	int result = 0;
	int startsec = blknum * SECTORCOUNT(sb->blkdev);
	int i;

	for(i=0; i < SECTORCOUNT(sb->blkdev); i++){
		DEVOP_READ(sb->blkdev, startsec + i, buf + (i * sb->blkdev->blk_size));
	}

	return result;
}

int ssufs_writeblock(struct ssufs_superblock *sb, uint32_t blknum, char *buf)
{
	int result = 0;
	int startsec = blknum * SECTORCOUNT(sb->blkdev);
	int i;

	for(i=0; i<SECTORCOUNT(sb->blkdev); i++)
		DEVOP_WRITE(sb->blkdev, startsec + i, buf + (i * sb->blkdev->blk_size));
	
	return result;
}

int ssufs_sync_superblock(struct ssufs_superblock *sb){
	int result = 0;

	ssufs_writeblock(sb, sb->lba, (char *)sb);

	return result;
}

int ssufs_sync_bitmapblock(struct ssufs_superblock *sb){
	int result = 0;

	ssufs_writeblock(sb, SSU_BITMAP_BLOCK(sb->lba), sb->blkmap);

	return result;
}

int ssufs_sync_inodetable(struct ssufs_superblock *sb){
	int result = 0;
	int i;

	for(i = 0 ; i < NUM_INODE_BLOCK; i++)
		ssufs_writeblock(sb, SSU_INODE_BLOCK(sb->lba)+i, ((char *)ssufs_inode_table+(i*SSU_BLOCK_SIZE)));

	return result;
}

int ssufs_sync(struct ssufs_superblock *sb){
	int result = 0;

	ssufs_sync_superblock(sb);

	ssufs_sync_bitmapblock(sb);
}

/********************************************************* inode start ************************************************************/

int ssufs_inode_write(struct ssufs_inode *inode, uint32_t offset, char *buf, uint32_t len){
	int result = 0;
	uint32_t blkoff;
	uint32_t resoff;
	int block_index;
	int i;

	if(offset > inode->i_size || len <= 0 || buf == NULL)
		return -1;

	for(i = offset; i < offset+len; i = ((blkoff+1)*SSU_BLOCK_SIZE))
	{
		blkoff = i / SSU_BLOCK_SIZE;
		resoff = i % SSU_BLOCK_SIZE;

		memset(tmpblock, 0, SSU_BLOCK_SIZE);
		if(blkoff < NUM_DIRECT){//direct
			if(inode->i_direct[blkoff] == 0){//need to bitmap_alloc
				block_index = bitmap_alloc(inode->ssufs_sb->inodemap);
				block_index += SSU_DATA_BLOCK(inode->ssufs_sb->lba);

				if(block_index == -1){
					return -1;
				}

				inode->i_direct[blkoff] = block_index;
			}else{
				ssufs_readblock(inode->ssufs_sb, inode->i_direct[blkoff], tmpblock);

				struct dirent dirent;
				memcpy(&dirent, tmpblock, sizeof(dirent));
			}

			memcpy(tmpblock+resoff, buf, len);
			ssufs_writeblock(inode->ssufs_sb, inode->i_direct[blkoff], tmpblock);
		}else{//indirect,. not used

		}
	}

	inode->i_size = offset+len;

	ssufs_sync_bitmapblock(inode->ssufs_sb);
	ssufs_sync_inodetable(inode->ssufs_sb);

	return result;
}

int ssufs_inode_read(struct ssufs_inode *inode, uint32_t offset, char *buf, uint32_t len)
{
	int result = 0;
	uint32_t blkoff;
	uint32_t resoff;
	int i;

	if(offset > inode->i_size)
		return -1;

	for(i=0; i < len; i+=(SSU_BLOCK_SIZE - resoff)){
		blkoff = offset / SSU_BLOCK_SIZE;
		resoff = offset % SSU_BLOCK_SIZE;

		if(blkoff < NUM_DIRECT){//direct
			ssufs_readblock(inode->ssufs_sb, inode->i_direct[blkoff], tmpblock);
			memcpy(buf, tmpblock+resoff, MIN(SSU_BLOCK_SIZE - resoff, len));
		}else{//indirect, #not used

		}
	}

	return result;
}

struct ssufs_inode *inode_alloc(uint32_t type){
	struct ssufs_inode *cwd_inode = (struct ssufs_inode*)(cur_process->cwd->info);
	struct ssufs_superblock *sb = cwd_inode->ssufs_sb;
	struct ssufs_inode *inode;
	int i;

	for(i = INODE_ROOT; i < NUM_INODE; i++){
		if(!bitmap_test(sb->inodemap, i)){
			inode = &ssufs_inode_table[i];
			inode->i_no = i;
			inode->i_size = 0;
			inode->i_type = type;
			inode->i_refcount = 1;
			inode->ssufs_sb = sb;

			bitmap_set(sb->inodemap, i, true);

			ssufs_sync_inodetable(sb);
			ssufs_sync_bitmapblock(sb);

			return inode;
		}
	}

	return NULL;
}

/********************************************************* inode end ************************************************************/

struct vnode *make_vnode_tree(struct ssufs_superblock *sb, struct vnode *mnt_root)
{
	struct ssufs_inode *root_inode = &ssufs_inode_table[2];
	set_vnode(mnt_root, mnt_root, root_inode);
	mapping(mnt_root, root_inode);
	return mnt_root;
}

static int num_direntry(struct ssufs_inode *inode) //들어온 inode의 디렉토리 엔트리 개수 리턴
{
	if(inode->i_size % sizeof(struct dirent) != 0 || inode->i_type != SSU_DIR_TYPE)
		return -1;

	return inode->i_size / sizeof(struct dirent);
}
void mapping(struct vnode *parent_vnode, struct ssufs_inode *parent_inode) {
	uint32_t dirent_num = num_direntry(parent_inode);
	dirent_num -= 2;
	if(dirent_num > 0) {
		for(int i = 0; i < dirent_num; i++) {
			struct dirent dirent;
			struct vnode *child_vnode = vnode_alloc();

			ssufs_inode_read(parent_inode, (uint32_t)(sizeof(struct dirent)*(i+2)), 
					(char *)&dirent, (uint32_t)(sizeof(struct dirent)));

			set_vnode(child_vnode, parent_vnode,&ssufs_inode_table[dirent.d_ino]);
			list_push_back(&parent_vnode->childlist, &child_vnode->elem);
			mapping(child_vnode, &ssufs_inode_table[dirent.d_ino]);
		}
	}
}
void set_vnode(struct vnode *vnode, struct vnode *parent_vnode, struct ssufs_inode *inode)//
{
	struct dirent dirent;
	struct ssufs_inode *parent_inode;
	int i, ndir;

	get_curde(inode, &dirent);
	memcpy(vnode->v_name, dirent.d_name, LEN_VNODE_NAME);

	vnode->v_parent = parent_vnode;
	vnode->type = inode->i_type;
	vnode->v_op.mkdir = ssufs_mkdir;
	vnode->v_op.ls = NULL;

	vnode->info = (void *)inode;
}

int get_curde(struct ssufs_inode *cwd, struct dirent * de)
{
	struct ssufs_inode *pwd;
	int i, ndir;

	//get parent dir
	ssufs_inode_read(cwd, 0, (char*)de, sizeof(struct dirent));
	pwd = &ssufs_inode_table[de->d_ino];
	ndir = num_direntry(pwd);

	for(i=0; i<ndir; i++)
	{
		ssufs_inode_read(pwd, i*sizeof(struct dirent), (char*)de, sizeof(struct dirent));
		if(de->d_ino == cwd->i_no)
			return 0;
	}
	return -1;
}

//**************************************************     vnode operation      *****************************************************/
int ssufs_mkdir(char *dirname){
	struct ssufs_inode *cwd_inode = (struct ssufs_inode *)(cur_process->cwd->info);
	struct ssufs_inode *new_dir_inode = inode_alloc(SSU_DIR_TYPE);
	struct vnode *parent_vnode = cur_process->cwd;
	struct vnode *child_vnode = vnode_alloc();
	struct dirent dirent;
	struct list_elem *e;
	uint32_t offset_cwd_dir = num_direntry(cwd_inode);
	uint32_t offset_new_dir = num_direntry(new_dir_inode);

	for(e = list_begin(&cur_process->cwd->childlist); e != list_end(&cur_process->cwd->childlist);
				e = list_next(e)) {
		struct vnode *child = list_entry(e, struct vnode, elem);
		if(strcmp(dirname, child->v_name) == 0) {
			printk("you already have %s dir!\n", dirname);
			return 0;
		}
	}


	dirent.d_ino = new_dir_inode->i_no;
	dirent.d_type = SSU_DIR_TYPE;
	memcpy(dirent.d_name, dirname, FILENAME_LEN);
	ssufs_inode_write(cwd_inode, (uint32_t)offset_cwd_dir*sizeof(struct dirent), (char *)&dirent, (uint32_t)sizeof(struct dirent));
	
	memcpy(dirent.d_name, "..", sizeof(".."));
	dirent.d_ino = cwd_inode->i_no;
	ssufs_inode_write(new_dir_inode, 0, (char *)&dirent, sizeof(struct dirent));
	memcpy(dirent.d_name, ".", sizeof("."));
	dirent.d_ino = new_dir_inode->i_no;
	ssufs_inode_write(new_dir_inode, (uint32_t)(sizeof(struct dirent)), (char *)&dirent, sizeof(struct dirent));

	set_vnode(child_vnode, parent_vnode, new_dir_inode);
	child_vnode->v_parent = parent_vnode;
	list_push_back(&parent_vnode->childlist, &child_vnode->elem);

	return 0;
}
