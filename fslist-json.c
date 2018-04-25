
#include "lester.h"
#include "lustre_lov.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>


void insert_in(json_t *j, json_t *val, char *key)
{
	json_object_set(j, key, val);
	json_decref(val);
}


json_t *build_lma(struct ea_info *lma)
{
	struct lustre_mdt_attrs *lma_val;
	json_t *res;

	if (!lma)
		return json_null();

	lma_val = lma->value;
	res = json_object();

	insert_in(res, json_integer(lma_val->lma_self_fid.f_seq), "sequence");
	insert_in(res, json_integer(lma_val->lma_self_fid.f_oid), "oid");
	insert_in(res, json_integer(lma_val->lma_self_fid.f_ver), "version");

	return res;
}

json_t *build_osts_parts(struct ea_info *lov, struct ext2_inode *ino)
{
	struct lov_mds_md_v1 *lov1;
	struct lov_mds_md_v3 *lov3;
	struct lov_ost_data_v1 *ost;
	int cnt;
	json_t *res, *objs;

	if(!lov || !LINUX_S_ISREG(ino->i_mode))
		return json_null();

	lov1 = lov->value;
	if (lov1->lmm_magic == LOV_MAGIC_V1) {
		cnt = lov1->lmm_stripe_count;
		ost = lov1->lmm_objects;
	} else if (lov1->lmm_magic == LOV_MAGIC_V3) {
		lov3 = lov->value;
		cnt = lov3->lmm_stripe_count;
		ost = lov3->lmm_objects;
	} else {
		return json_null();
	}

	if (!cnt)
		return json_null();

	res = json_array();

	ost--;
	objs = json_object();
	insert_in(objs, json_integer(ost[cnt].l_ost_idx), "ostID");
	insert_in(objs, json_integer(ost[cnt].l_object_id), "objectID");
	json_array_append(res, objs);
	json_decref(objs);
	while(--cnt) {
		objs = json_object();
		insert_in(objs, json_integer(ost[cnt].l_ost_idx), "ostID");
		insert_in(objs, json_integer(ost[cnt].l_object_id), "objectID");
		json_array_append(res, objs);
		json_decref(objs);
	}
	return res;
}

json_t *build_ea(struct ea_info *lov, struct ea_info *lma, struct ext2_inode *ino)
{
	json_t *res = json_object();
	insert_in(res, build_lma(lma), "lma");
	insert_in(res, build_osts_parts(lov, ino), "lov");
	return res;
}

int fslist_JSoutput(FILE *f, ext2_ino_t ino, struct ext2_inode *inode,
			int offset, const char *name, int namelen,
			struct ea_info *lov, struct ea_info *lma)
{
	json_t *res;
	char *path;

	path = (char*)malloc(sizeof(char) * (offset + namelen + 2));

	sprintf(path, "%.*s%.*s", offset, path_buffer, namelen, name);
	
	res = json_object();

	//time
	insert_in(res, json_integer(inode->i_ctime), "ctime");
	insert_in(res, json_integer(inode->i_atime), "atime");
	insert_in(res, json_integer(inode->i_mtime), "mtime");
	//userinfo
	insert_in(res, json_integer(inode_gid(*inode)), "gid");
	insert_in(res, json_integer(inode_uid(*inode)), "uid");
	insert_in(res, json_integer(inode->i_mode), "mode");
	//inode
	insert_in(res, json_integer(EXT2_I_SIZE(inode)), "inodesize");
	insert_in(res, json_integer(ino), "inode");
	insert_in(res, json_string(path), "path");
	//"options"
	insert_in(res, json_integer(inode->i_links_count), "nlink");
	insert_in(res, json_integer(inode->i_blocks), "blocks");
	//ea
	//anyway we can deal with those info in robinhood
	insert_in(res, build_ea(lov, lma, inode), "ea");

	json_dumpf(res, f, 0);//maybe later we may use flags

	free(path);
	json_decref(res);

	fprintf(f, "\n");
}
