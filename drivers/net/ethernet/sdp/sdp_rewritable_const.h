/*
 * Create by drain.lee
 *
 * 20150722 drain.lee	create
 */
#ifdef CONFIG_USE_REWRITABLE_CONST
#define REWRITABLE_CONST
#endif

#ifdef REWRITABLE_CONST

#define DEF_RWCONST_U32(_name, _value, _desc) \
const struct { \
	const u32 val; \
	const char magic[8]; \
	const u32 def_val; \
	const char desc[sizeof(#_name ": " _desc)]; \
} _##_name = { \
	.val = _value, \
	.magic = "RWCONST\x01", \
	.def_val = _value, \
	.desc = #_name ": " _desc, \
}
#define REF_RWCONST_U32(_name)	(*(volatile u32 *)&(_##_name.val))

#else/*REWRITABLE_CONST*/

#define DEF_RWCONST_U32(_name, _hex, _desc)	const u32 _##_name = _hex
#define REF_RWCONST_U32(_name)	(_##_name)

#endif/*REWRITABLE_CONST*/

#define DEF_EDITED_VALUES()     DEF_RWCONST_U32(REWRITABLE_CONST_EDITED_VALUES, 0x0, "##SPECIAL##")
#define REF_EDITED_VALUES()     REF_RWCONST_U32(REWRITABLE_CONST_EDITED_VALUES)

