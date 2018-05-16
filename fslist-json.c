
#include "lester.h"
#include "lustre_attr.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>


#define HSM_RELEASED	0x8000000 /*with lfs hsm_state*/
//#define HSM_RELEASE	1 << 12 /*in the lustre user api*/

void insert_in(json_t *j, json_t *val, char *key)
{
	json_object_set(j, key, val);
	json_decref(val);
}

json_t *build_fid(struct lu_fid f)
{
	json_t *res;
	
	res = json_object();

	insert_in(res, json_integer(f.f_seq), "sequence");
	insert_in(res, json_integer(f.f_oid), "oid");
	insert_in(res, json_integer(f.f_ver), "version");

	return res;
}

json_t *build_lma(struct ea_info *lma)
{
	struct lustre_mdt_attrs *lma_val;

	if (!lma)
		return json_null();

	lma_val = lma->value;


	return build_fid(lma_val->lma_self_fid);
}

json_t *build_osts_parts(struct ea_info *lov, struct ext2_inode *ino)
{
	struct lov_mds_md_v1 *lov1;
	struct lov_mds_md_v3 *lov3;
	struct lov_ost_data_v1 *ost;
	int cnt;
	size_t size;
	json_t *tab, *res, *objs;

	if	(!lov || !LINUX_S_ISREG(ino->i_mode))
		return json_null();

	lov1 = lov->value;
	if (lov1->lmm_magic == LOV_MAGIC_V1) {
		cnt = lov1->lmm_stripe_count;
		size = lov1->lmm_stripe_size;
		ost = lov1->lmm_objects;
	} else if (lov1->lmm_magic == LOV_MAGIC_V3) {
		lov3 = lov->value;
		cnt = lov3->lmm_stripe_count;
		size = lov3->lmm_stripe_size;
		ost = lov3->lmm_objects;
	} else {
		return json_null();
	}

	if (!cnt)
		return json_null();

	res = json_object();
	insert_in(res, json_integer(cnt), "stripecount");
	insert_in(res, json_integer(size), "stripesize");


	tab = json_array();

	ost--;
	objs = json_object();
	insert_in(objs, json_integer(ost[cnt].l_ost_idx), "ostID");
	insert_in(objs, json_integer(ost[cnt].l_object_id), "objectID");
	insert_in(objs, json_integer(ost[cnt].l_ost_gen), "ostgen");
	json_array_append(res, objs);
	json_decref(objs);
	while(--cnt) {
		objs = json_object();
		insert_in(objs, json_integer(ost[cnt].l_ost_idx), "ostID");
		insert_in(objs, json_integer(ost[cnt].l_object_id), "objectID");
		insert_in(objs, json_integer(ost[cnt].l_ost_gen), "ostgen");
		json_array_append(res, objs);
		json_decref(objs);
	}
	insert_in(res, tab, "stripeparts");
	return res;
}

json_t *build_hsm(struct ea_info *hsm)
{
	json_t *res;
	struct hsm_t *val;

	if(!hsm)
		return json_null();

	val = hsm->value;
	res = json_object();

	insert_in(res, json_integer(val->unused), "unused");
	insert_in(res, json_integer(val->flags), "flags");
	insert_in(res, json_integer(val->archive_id), "archiveid");
	insert_in(res, json_integer(val->archived_version), "archivedversion");

	return res;
}

json_t *build_linkea(struct ea_info *linkea)
{
	int len, n;
	char *temp;
	json_t *res, *obj;
	struct lu_fid fid;
	struct linkea_data dat = { 0 };
	struct lu_buf lb = { 0 };

	if(linkea == NULL)
		return json_null();

	lb.lb_buf = linkea->value;
	lb.lb_len = linkea->value_len;
	dat.ld_buf = &lb;
	dat.ld_leh = (struct link_ea_header *)linkea->value;

	dat.ld_lee = LINKEA_FIRST_ENTRY(dat);
	dat.ld_reclen = (dat.ld_lee->lee_reclen[0] << 8) 
			| dat.ld_lee->lee_reclen[1];

	if(dat.ld_leh->leh_reccount <= 0)
		return json_null();

	res = json_array();

	obj = json_object();
	len = dat.ld_reclen - sizeof(struct link_ea_entry);
	temp = malloc(sizeof(char) * (len + 1));
	temp = strncpy(temp, dat.ld_lee->lee_name, len);
	temp[len] = '\0';
	insert_in(obj, json_string(temp), "name");
	memcpy(&fid, &dat.ld_lee->lee_parent_fid, sizeof(fid));
	fid_be_to_cpu(&fid, &fid);
	insert_in(obj, build_fid(fid), "pfid");
	json_array_append(res, obj);
	json_decref(obj);
	free(temp);

	for(n = 1; n < dat.ld_leh->leh_reccount; n++)
	{
		obj = json_object();
		dat.ld_lee = LINKEA_NEXT_ENTRY(dat);
		dat.ld_reclen = (dat.ld_lee->lee_reclen[0] << 8) 
				| dat.ld_lee->lee_reclen[1];
		//
		len = dat.ld_reclen - sizeof(struct link_ea_entry);
		temp = malloc(sizeof(char) * (len + 1));
		temp = strncpy(temp, dat.ld_lee->lee_name, len);
		temp[len] = '\0';
		insert_in(obj, json_string(temp) ,"name");
		memcpy(&fid, &dat.ld_lee->lee_parent_fid, sizeof(fid));
		fid_be_to_cpu(&fid, &fid);
		insert_in(obj, build_fid(fid), "pfid");//WARNING
		json_array_append(res, obj);
		json_decref(obj);
		free(temp);
	}
	return res;

}

/*later we test diferrent things here*/
json_t *build_ea(struct ea_info *lov, struct ea_info *lma, struct ea_info *hsm,
			struct ea_info *linkea, struct ext2_inode *ino)
{
	json_t *res = json_object();

	/*first is the file released?*/
	insert_in(res, build_lma(lma), "lma");
	insert_in(res, build_osts_parts(lov, ino), "lov");
	insert_in(res, build_hsm(hsm), "hsm");
	insert_in(res, build_linkea(linkea), "linkea");
	return res;
}

int fslist_JSoutput(FILE *f, ext2_ino_t ino, struct ext2_inode *inode,
			int offset, const char *name, int namelen,
			struct ea_info *lov, struct ea_info *lma, struct ea_info *hsm, 
			struct ea_info *linkea)
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
	insert_in(res, build_ea(lov, lma, hsm, linkea, inode), "ea");

	json_dumpf(res, f, 0);//maybe later we may use flags

	free(path);
	json_decref(res);

	fprintf(f, "\n");
}
