"""
 *  smemsim.py (Samsung DTV Soc SMEM sim app)
 *  author : drain.lee@samsung.com
 *  version 20170404 add memory state graph and code refactoring
"""

from collections import namedtuple
from collections import OrderedDict
import random
import unittest
import logging
import operator
import itertools

class SMEMInfo:
    def __init__(self, regions, sizes):
        self.regions = regions
        self.sizes = sorted(sizes, reverse=True)
        self.handleInfos = [0] * len(sizes)

    def count(self):
        return len(self.sizes)

    def getSize(self, idx):
        return self.sizes[idx]

    def getHandle(self, idx):
        return self.handleInfos[idx]

    def setHandle(self, idx, handle):
        del(self.handleInfos[idx])
        self.handleInfos.insert(idx, handle)

    def get_regions(self):
        return '+'.join(self.regions)

    def get_sizes(self):
        return self.sizes

    def __repr__(self):
        return "SMEMInfo Region{}: {}".format(self.region, self.sizes)


Resource = namedtuple("Resource", ["name", "group", "weight", "flags", "SMEMInfos"])
Scenario = namedtuple("Scenario", ["name", "group", "resources"])

'''
smem alloc flags:
#define SMEMFLAGS_HEAP_DEFAULT		(0x00)
#define SMEMFLAGS_SHARED_SYSTEM		(1)
#define SMEMFLAGS_CARVEOUT_DYNAMIC	(2)
#define SMEMFLAGS_CARVEOUT_STATIC	(4)
#define SMEMFLAGS_HEAPTYPE(flags)	((flags) & 0xff)

#define SMEMFLAGS_POS_DEFAULT		(0x0000)
#define SMEMFLAGS_POS_TOP		(0x0100)
#define SMEMFLAGS_POS_MID		(0x0200)
#define SMEMFLAGS_POS_BOTTOM		(0x0400)
#define SMEMFLAGS_POS(flags)		((flags) & 0xff00)
'''
resources = {
    # MFC {A 10 + B 31.5} * 2 = {A 20 + B 73}
    #                                                                                 VI
    "MFC0_DTV":    Resource("MFC0_DTV",     0,  40, 0x0302, (SMEMInfo(["A"], [1.0, 10.0]),  SMEMInfo(["B"], [8.5+ 23.0]),  SMEMInfo(["C"], []))),
    "MFC0_MM":     Resource("MFC0_MM",      0,  40, 0x0302, (SMEMInfo(["A"], [1.0, 10.0]),  SMEMInfo(["B"], [4.5+ 25.0]),  SMEMInfo(["C"], []))),
    "MFC1_DTV":    Resource("MFC1_DTV",     0,  40, 0x0302, (SMEMInfo(["A"], [1.0, 10.0]),  SMEMInfo(["B"], [8.5+ 23.0]),  SMEMInfo(["C"], []))),
    "MFC1_MM":     Resource("MFC1_MM",      0,  40, 0x0302, (SMEMInfo(["A"], [1.0, 10.0]),  SMEMInfo(["B"], [4.5+ 26.0]),  SMEMInfo(["C"], []))),
                                                                  
    # DVDE (A 113.5+31, B 122+32} = {A 144.5 + B 154 }            
    "DVDE0_HEVC":  Resource("DVDE0_HEVC",   0,  60, 0x0302, (SMEMInfo(["A"], [13.0+ 88.5]),  SMEMInfo(["B"], [8.0, 33.0+ 88.5]), SMEMInfo(["C"], []))),
    "DVDE0_H264":  Resource("DVDE0_H264",   0,  60, 0x0302, (SMEMInfo(["A"], [40.0+ 70.5]),  SMEMInfo(["B"], [8.0, 33.0+ 70.0]), SMEMInfo(["C"], []))),
    "DVDE0_VP9":   Resource("DVDE0_VP9",    0,  60, 0x0302, (SMEMInfo(["A"], [25.0+ 88.5]),  SMEMInfo(["B"], [8.0, 33.0+ 90.0]), SMEMInfo(["C"], []))),
    "DVDE1_HEVC":  Resource("DVDE1_HEVC",   0,  40, 0x0302, (SMEMInfo(["A"], [ 5.0+ 23.0]),  SMEMInfo(["B"], [1.0,  8.5+ 23.0]), SMEMInfo(["C"], []))),
    "DVDE1_VP9":   Resource("DVDE1_VP9",    0,  40, 0x0302, (SMEMInfo(["A"], [ 8.0+ 23.0]),  SMEMInfo(["B"], [1.0,  8.5+ 23.5]), SMEMInfo(["C"], []))),
                                                            
    "JPEG_UHD":    Resource("JPEG_UHD",     0, 100, 0x0302, (SMEMInfo(["A"], [32.0+ 32.0 + 32.0]),SMEMInfo(["B"], [32.0+ 32.0+ 32.0+ 8.0]), SMEMInfo(["C"], []))),
    "MJPEG_UHD":   Resource("MJPEG_UHD",    0, 100, 0x0302, (SMEMInfo(["A"], [51.0+ 51.0]),       SMEMInfo(["B"], [8.0]), SMEMInfo(["C"], []))),
    "MJPEG_FHD":   Resource("MJPEG_FHD",    0,  40, 0x0302, (SMEMInfo(["A"], [ 4.0+ 8.8+ 17.0]),  SMEMInfo(["B"], []), SMEMInfo(["C"], []))),
    "JPEG_FHD":    Resource("JPEG_FHD",     0,  40, 0x0302, (SMEMInfo(["A"], [ 1.0+ 8.0]),        SMEMInfo(["B"], [8.0]), SMEMInfo(["C"], []))),

    "DP_M_EXT422" : Resource("DP_M_EXT422", 1, 100, 0x0402, (SMEMInfo(["A"], [48.5]),      SMEMInfo(["B"], []),    SMEMInfo(["C"], []))),
    "DP_M_DEC420" : Resource("DP_M_DEC420", 1, 100, 0x0402, (SMEMInfo(["A"], [46.5]),      SMEMInfo(["B"], []),    SMEMInfo(["C"], []))),    
    "DP_M_DECFHD" : Resource("DP_M_DEC420", 1, 100, 0x0402, (SMEMInfo(["A"], [26.5]),      SMEMInfo(["B"], []),     SMEMInfo(["C"], []))),
    "DP_M_DECPROT": Resource("DP_M_DEC420", 1, 100, 0x0402, (SMEMInfo(["A"], [37.0]),      SMEMInfo(["B"], []),    SMEMInfo(["C"], []))),    
    "DP_M_PC":      Resource("DP_M_PC",     1, 100, 0x0402, (SMEMInfo(["A"], [42.5]),      SMEMInfo(["B"], []),    SMEMInfo(["C"], []))),
    
    "DP_S_DEC420": Resource("DP_S_DEC420",  2, 100, 0x0402, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []),        SMEMInfo(["C"], [42.5+ 25.5]))),
    "DP_S_CLONE":  Resource("DP_S_CLONE",   2, 100, 0x0402, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []),        SMEMInfo(["C"], [21.5+ 27.5]))),

    "ENCODER":     Resource("ENCODER",      3, 100, 0x0000, (SMEMInfo(["A"], [3.5, 6.8]),  SMEMInfo(["B"], [7.0]), SMEMInfo(["C"], []))),
    "ATV":         Resource("ATV",          4, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], [6.0]), SMEMInfo(["C"], []))),
    "CHDEC":       Resource("CHDEC",        5, 100, 0x0000, (SMEMInfo(["A"], [8.5]),       SMEMInfo(["B"], []), SMEMInfo(["C"], []))),
    "GFX1":        Resource("GFX1",         6, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [8.0, 8.0]))),
    "CURSOR":      Resource("CURSOR",       7, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [1.5]))),
    "TSD":         Resource("TSD",          8, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [3.0]))),
    "PVR0":        Resource("PVR0",         9,  50, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [7.0]))),
    "PVR1":        Resource("PVR1",         9,  50, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [7.0]))),
    "ALPD":        Resource("ALPD",        10, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], []), SMEMInfo(["C"], [1.0]))),
    "HDMI":        Resource("HDMI",        11, 100, 0x0000, (SMEMInfo(["A"], []),          SMEMInfo(["B"], [0.3]), SMEMInfo(["C"], []))),
}


scenarios = (
    Scenario("DTV_MBC", 0, (resources["MFC0_DTV"], resources["DP_M_DEC420"], resources["TSD"])),
    Scenario("DTV_ISDBTFHD", 0, (resources["MFC0_DTV"], resources["DP_M_DEC420"], resources["CHDEC"], resources["TSD"])),
    Scenario("DTV_DVBT2UHD", 0, (resources["DVDE0_HEVC"], resources["DP_M_DEC420"], resources["CHDEC"], resources["TSD"])),
    Scenario("ATV", 0, (resources["ATV"], resources["DP_M_EXT422"])),
    Scenario("FILE_HEVCUHD", 1, (resources["DVDE0_HEVC"], resources["DP_M_DEC420"])),
    Scenario("FILE_VP9UHD", 1, (resources["DVDE0_VP9"], resources["DP_M_DEC420"])),
    Scenario("FILE_H264UHD", 1, (resources["DVDE0_H264"], resources["DP_M_DEC420"])),
    Scenario("FILE_HEVCFHD", 1, (resources["DVDE1_HEVC"], resources["DP_M_DEC420"])),    
    Scenario("FILE_MPEG2", 1, (resources["MFC1_MM"], resources["DP_M_DECFHD"])),
    Scenario("FILE_MPEG2_ROT", 1, (resources["MFC1_MM"], resources["DP_M_DECPROT"])),    
    Scenario("SCREEN_MIRROR", 1, (resources["MFC1_MM"], resources["DP_M_DECFHD"])),
    Scenario("JPEG_UHD", 1, (resources["JPEG_UHD"], resources["DP_M_PC"])),
    Scenario("MJPEG_FHD", 1, (resources["MJPEG_FHD"], resources["DP_M_PC"])),
    Scenario("MJPEG_UHD", 1, (resources["MJPEG_UHD"], resources["DP_M_PC"])),
    Scenario("HDMI", 2, (resources["HDMI"], resources["DP_M_EXT422"])),
    Scenario("HDMIPC", 2, (resources["HDMI"], resources["DP_M_PC"])),
    Scenario("TV2MOBILE", 3, (resources["DP_S_CLONE"], resources["ENCODER"])),
    #   Scenario(   "TV2MOBILE_OFF",    3,()),
    Scenario("DTV_PIP", 4, (resources["MFC1_DTV"], resources["DP_S_DEC420"])),
    #   Scenario(   "DTV_PIP_OFF",      4   , ()),
    Scenario("RETAIL_MODE", 5, (resources["GFX1"],)),
    #   Scenario(   "RETAIL_MODE_OFF",  5   , ()),
    Scenario("PVR_REC", 6, (resources["PVR0"], resources["TSD"])),
    #   Scenario(   "PVR_REC_OFF",      6   , ()),
    Scenario("PVR_RECDUAL", 7, (resources["PVR0"], resources["PVR1"], resources["TSD"])),
    #   Scenario(   "PVR_RECDUAL_OFF",  7   , ()),
    Scenario("WEBBROWSER", 8, (resources["CURSOR"],)),
    #   Scenario(   "WEBBROWSER_OFF",   8   , ()  ),
)


class BasicMEM:
    def __init__(self, regionInfos, order=7):
        self.__region_infos = regionInfos
        self._unit = 0x1000 << order
        self._memory_infos = {}

        for region, (ridx, rinfo) in self.__region_infos.items():
            chunks = range(rinfo[0], rinfo[0] + rinfo[1], self._unit)
            minfos = dict(zip(chunks, [None] * len(chunks))).items()
            
            # 24 Apr 2017: not use sorted alloc order
            #sorted_minfos = sorted(minfos)
            sorted_minfos = minfos
            
            self._memory_infos[region] = OrderedDict(sorted_minfos)

    def __repr__(self):
        view = ""
        for region, meminfo in sorted(self._memory_infos.items()):
            view += "[Region{} Status]\n".format(region)
            char = ord('A') - 1
            pre_state = None
            for chunk, state in meminfo.items():
                if state is not None:
                    if pre_state != state:
                        char += 1
                    view += chr(char)
                    pre_state = state
                else:
                    view += "-"
            view += "\n"
        return view

    def regions_to_mask(self, regions):
        mask = 0
        for region in regions:
            idx = self.__region_infos[region][0]
            mask = mask | (1 << idx)
        return mask

    def mask_to_regions(self, region_mask):
        regions = []
        for region, (ridx, rinfo) in self.__region_infos.items():
            if region_mask & (0x1 << ridx):
                regions.append(region)
        return tuple(sorted(regions))

    def set_alloc(self, start, size, reqinfos):
        for region, (ridx, (rstart, rsize)) in self.__region_infos.items():
            if (start >= rstart) and ((start + size) <= (rstart + rsize)):
                meminfo = self._memory_infos[region]
                info = (hex(start), hex(size), reqinfos)
                for chunk in range(start, start + size, self._unit):
                    if meminfo[chunk] is None:
                        meminfo[chunk] = info
                    else:
                        msg = "memory chunk({}) is not free {}!".format(hex(chunk), meminfo[chunk])
                        raise Exception(msg)
                print " -> ", hex(start), hex(size), reqinfos
                return
        raise Exception("can't find fit region")

    def set_free(self, start, size):
        for region, (ridx, (rstart, rsize)) in self.__region_infos.items():
            if (start >= rstart) and ((start + size) <= (rstart + rsize)):
                meminfo = self._memory_infos[region]
                for chunk in range(start, start + size, self._unit):
                    if meminfo[chunk] is not None:
                        meminfo[chunk] = None
                    else:
                        raise Exception("memory chunk({}) is already free!".format(hex(chunk)))
                print " -> ", hex(start), hex(size)
                return
        raise Exception("can't find fit region")

    def get_alloc_sizes(self):
        alloc_sizes = {}
        for region, meminfo in self._memory_infos.items():
            sum = 0
            for state in meminfo.values():
                if state is not None:
                    sum += self._unit
            alloc_sizes[region] = sum
        return tuple(sorted(alloc_sizes.items(), key=operator.itemgetter(0)))

    def get_alloc_infos(self):
        alloc_infos = {}
        for region, meminfo in self._memory_infos.items():
            infos = []
            pre_state = None
            for state in meminfo.values():
                if (state is not None) and (state is not pre_state):
                    infos.append(state)
                    pre_state = state
            alloc_infos[region] = infos
        return tuple(sorted(alloc_infos.items(), key=operator.itemgetter(0)))

    def alloc(self, desc, reqsize, align, flags, regions):
        raise NotImplementedError

    def free(self, handle):
        raise NotImplementedError

    '''return handle list'''
    def get_handles(self):
        raise NotImplementedError

    def get_handle_info(self, handle):
        raise NotImplementedError


class FakeSMEM(BasicMEM):
    def __init__(self, regionInfos, order=7):
        BasicMEM.__init__(self, regionInfos, order)

        print "FakeSMEM __init__"
        self.__nextHandle = 0
        self.__alloc_dict = {}

    def __alloc_on_region(self, reqsize, align, flags, region):
        meminfo = self._memory_infos[region]

        startchunk = None
        for chunk, state in meminfo.items():
            if state is None:
                if startchunk is None:
                    startchunk = chunk
                alloc_size = (chunk - startchunk) + self._unit
                if alloc_size >= reqsize:
                    self.__alloc_dict[self.__nextHandle] = (
                        startchunk, alloc_size, (reqsize, align, flags, region))
                    return self.__nextHandle
            else:
                startchunk = None
        return None

    def alloc(self, desc, reqsize, align, flags, regions):
        print "FakeSMEM alloc", desc, hex(reqsize), hex(align), hex(flags), regions,
        self.__nextHandle += 1
        handle = None
        for rg in regions:
            handle = self.__alloc_on_region(reqsize, align, flags, rg)
            if handle is not None:
                break
        if handle is None:
            raise Exception("can not allocate memory!!!!")

        BasicMEM.set_alloc(self, self.__alloc_dict[handle][0], self.__alloc_dict[handle][1], (handle, desc, reqsize, regions))
        return handle

    def free(self, handle):
        print "FakeSMEM free", handle,

        if handle in self.__alloc_dict:
            startchunk, alloc_size, (reqsize, align, flags,
                                     region) = self.__alloc_dict.pop(handle)

            BasicMEM.set_free(self, startchunk, alloc_size)
        else:
            raise Exception("invalid handle({})".format(handle))

    def get_handles(self):
        return self.__alloc_dict.keys()

    def get_handle_info(self, handle):
        return self.__alloc_dict[handle]


class RealSMEM(BasicMEM):
    __path_alloc = "/sys/kernel/debug/smem-test/alloc"
    __path_free = "/sys/kernel/debug/smem-test/free"
    __path_regioninfo = "/sys/kernel/debug/smem-test/regioninfo"

    def __init__(self, order=7):
        # region: (idx, (start, size))
        '''
        0 0x11fe00000 0x12fffffff 0x010200000
        1 0x05c000000 0x06fffffff 0x014000000
        2 0x0cd400000 0x0e4afffff 0x017700000
        '''
        regionInfos = {}
        file = open(RealSMEM.__path_regioninfo, "r+")  # open read/write mode
        for i in range(100):
            line = file.readline()
            if not line:
                break
            # print(line)
            results = line.split()
            regidx = int(results[0], 10)
            start = int(results[1], 16)
            # end = int(results[2], 16)
            size = int(results[3], 16)

            regionInfos[chr(ord("A") + regidx)] = (regidx, (start, size))
            # print regionInfos
        file.close()

        BasicMEM.__init__(self, regionInfos, order)

        print "RealSMEM __init__"
        self.__lastAllocInfo = 0
        self.__alloc_dict = {}

    def alloc(self, desc, reqsize, align, flags, regions):
        print "RealSMEM alloc", desc, hex(reqsize), hex(align), hex(flags), regions,

        file = open(RealSMEM.__path_alloc, "r+")  # open read/write mode
        cmd = "{} 0x{:x} 0x{:x} 0x{:x} 0x{:x}".format(
            desc, reqsize, align, flags, self.regions_to_mask(regions))
        file.writelines(cmd)
        results = file.readline().split()
        file.close()

        handle = results[0]
        alloc_addr = int(results[1], 16)
        alloc_size = int(results[2], 16)

        self.__alloc_dict[handle] = (alloc_addr, alloc_size, (desc, hex(reqsize), hex(align), hex(flags), regions))

        BasicMEM.set_alloc(self, alloc_addr, alloc_size, (handle, desc, reqsize, regions))
        return handle

    def free(self, handle):
        print "RealSMEM free", handle,

        if handle in self.__alloc_dict:
            file = open(RealSMEM.__path_free, "r+")  # open write mode
            cmd = "{}".format(handle)
            file.writelines(cmd)
            file.close()

            alloc_addr, alloc_size, (desc, reqsize, align, flags, region_mask) = self.__alloc_dict.pop(handle)
            BasicMEM.set_free(self, alloc_addr, alloc_size)
        else:
            raise Exception("invalid handle({})".format(handle))

    def get_handles(self):
        return self.__alloc_dict.keys()

    def get_handle_info(self, handle):
        return self.__alloc_dict[handle]


class SMEMSim:

    def __init__(self, name, scenarios, allocater):
        self.name = name
        self.scenarios = scenarios[:]
        self.__allocater = allocater
        self.running_scenarios = []
        self.alloc_res_map_to_scen = {}  # key=res value=scen

    def get_scenarios(self):
        return self.scenarios[:]

    def get_runing_scenarios(self):
        return self.running_scenarios[:]

    def get_allocated_mapping(self):
        return self.alloc_res_map_to_scen.items()

    def get_total_req_size(self):
        req_sizes = {}
        for res in self.alloc_res_map_to_scen.keys():
            for smeminfos in res.SMEMInfos:
                for size in smeminfos.get_sizes():
                    if smeminfos.get_regions() in req_sizes:
                        req_sizes[smeminfos.get_regions()] += size * 1024 * 1024
                    else:
                        req_sizes[smeminfos.get_regions()] = size * 1024 * 1024

        return sorted(req_sizes.items(), key=operator.itemgetter(0))

    def get_total_alloc_size(self):
        return self.__allocater.get_alloc_sizes()

    def resource_alloc(self, new_scen, resource):
        logging.error("+R %s g%d w%d", resource.name,
                      resource.group, resource.weight)

        same_group = filter(lambda r: r.group == resource.group,
                            self.alloc_res_map_to_scen.keys())
        same_grp_weight = 0
        for res in same_group:
            same_grp_weight += res.weight

        for res in same_group[::-1]:
            if same_grp_weight + resource.weight > 100:
                logging.info("resource group {} weight over!({}+{}({}))! free {}({})".format(
                    resource.group, same_grp_weight, resource.name, resource.weight, res.name, res.weight))
                self.resource_free(res)
                same_grp_weight -= res.weight
            else:
                break

        for smeminfo in resource.SMEMInfos:
            if smeminfo.count() < 1:
                continue
            for idx in range(0, smeminfo.count()):
                handle = self.__allocater.alloc("{}".format(resource.name), int(smeminfo.getSize(idx) * 1024 * 1024), 0, resource.flags, smeminfo.get_regions())
                smeminfo.setHandle(idx, handle)
        self.alloc_res_map_to_scen[resource] = new_scen
        return

    def resource_free(self, resource):
        logging.error("-R %s", resource.name)

        if resource not in self.alloc_res_map_to_scen.keys():
            logging.critical("%s is not allocated", resource.name)
            return

        for smeminfo in resource.SMEMInfos:
            if smeminfo.count() < 1:
                continue
            for idx in range(0, smeminfo.count()):
                self.__allocater.free(smeminfo.getHandle(idx))
                smeminfo.setHandle(idx, 0)
        self.alloc_res_map_to_scen.pop(resource)
        return

    def is_dpm_resource(self, res):
        return res.name.find('DP_M') == 0

    def scenario_start(self, new_scen):
        if new_scen in self.running_scenarios:
            return

        logging.error("+S %s(%d)", new_scen.name, new_scen.group)
        print "scenario_start {}".format(new_scen.name)
        
        # 20 Apr 2017: this strategy is cancelled
        '''
                # DP_M_XXX resource conflict cases:
                #  same scenario group && conflicted same DP format resources: old scenario's DP_M_XXX _cannot_ be freed
                #  in any other cases on DP_M_XXX's conflicts, old scenario's DP_M_XXX _can_ be freed earlier than any ohter new allocations
                old_dpm_resources = filter(lambda r: self.is_dpm_resource(r), self.alloc_res_map_to_scen.keys())
                new_dpm_resources = filter(lambda r: self.is_dpm_resource(r), new_scen.resources)
                dpm_to_freed = set([])

                for old_r, new_r in itertools.product(old_dpm_resources, new_dpm_resources):
                    old_scen = self.alloc_res_map_to_scen[old_r]
                    conflicted = old_scen.group != new_scen.group or new_r != old_r
                    if (conflicted):
                        dpm_to_freed.add(old_r)
                    logging.info("DP resource conflict check,  {}::{} --> {}::{} = {}".format(
                            old_scen.name, old_r.name, new_scen.name, new_r.name, conflicted))

                for r in dpm_to_freed:
                    self.resource_free(r)
        '''
        holdnames = ""
        allocated_resources = self.alloc_res_map_to_scen.keys()
        hold_resources = list(set(new_scen.resources) & set(allocated_resources))

        # hold_resources = []
        for res in hold_resources:
            self.alloc_res_map_to_scen[res] = new_scen
            holdnames += res.name + " "
        if holdnames:
            logging.info("%s - Hold resource %s", new_scen.name, holdnames)
            print "ALREADY alloc resource", holdnames, "for", new_scen.name

        # stop conflicted scenarios
        for rs in filter(lambda r: r.group == new_scen.group, self.running_scenarios):
            logging.info("new scenario {}({}) start. same group stop {}".format(
                new_scen.name, new_scen.group, rs.name))
            print "FREE group{}".format(new_scen.group),
            self.scenario_stop(rs)

        for res in new_scen.resources:
            if res not in self.alloc_res_map_to_scen.keys():
                self.resource_alloc(new_scen, res)
        self.running_scenarios.insert(0, new_scen)
        return

    def scenario_stop(self, scen):
        logging.error("-S %s", scen.name)
        print "scenario_stop {}".format(scen.name)

        if scen not in self.running_scenarios:
            logging.critical("%s is not running scen", scen.name)
            return

        for res in scen.resources:
            if res in self.alloc_res_map_to_scen:
                if self.alloc_res_map_to_scen[res] == scen:
                    self.resource_free(res)
        self.running_scenarios.remove(scen)

        return


class SMEMSimTest(unittest.TestCase):

    # def __init__(self, SMEMSim):
    def setUp(self):
        print "\n\n\nTest Start!\n"

        self.turn = 0
        self.max_turn = 5000
        # region: (idx, (start, size))

        '''
        self.memory = FakeSMEM({
            "A": (0, (0x11FE00000, 0x10200000)),
            "B": (1, (0x05C000000, 0x14000000)),
            "C": (2, (0x0CD400000, 0x17700000)),
        })'''
        self.memory = RealSMEM()
        self.SMEMSim = SMEMSim("test", scenarios, self.memory)
        self.alloc_max = {}

    def tearDown(self):

        print "\n\n"
        print "[Turn {}/{}]".format(self.turn, self.max_turn)
        print "[requested Cur Size] ", map(lambda alloc: (alloc[0], alloc[1] / 1024 / 1024), self.SMEMSim.get_total_req_size())
        print "[allocated Cur Size] ", map(lambda alloc: (alloc[0], alloc[1] / 1024 / 1024), self.SMEMSim.get_total_alloc_size())
        print "[allocated MAX Size] ", map(lambda alloc: (alloc[0], alloc[1] / 1024 / 1024), sorted(self.alloc_max.items(), key=operator.itemgetter(0)))

        print self.memory

        print "[allocated resource mapping] Scenario : Resource"
        alloc_map = self.SMEMSim.get_allocated_mapping()
        sortlist = []
        for res in alloc_map:
            # print "{:20} :{}".format(res[1].name, res[0].name)
            sortlist.append((res[1].name, res[0].name))
        sortlist.sort()
        for res in sortlist:
            print "{:20} :{}".format(res[0], res[1])

        print "\n[allocated SMEM] region, [(start, size infos), ...]"
        for alloced in self.memory.get_alloc_infos():
            print alloced

        runing = self.SMEMSim.get_runing_scenarios()
        print "[All stop running scenarios(total {})!]".format(len(runing))
        for s in runing:
            self.SMEMSim.scenario_stop(s)

        print "[all free SMEM] handle, infos"
        for handle in self.memory.get_handles():
            self.memory.free(handle)

        for region, alloc_info in self.memory.get_alloc_infos():
            self.assertEqual(len(alloc_info), 0, "SMEM region{} alloc is not clear!\nalloc_info {}".format(region, alloc_info))

        del(self.SMEMSim)
        print "Test Done!"

    def test_AllScenarioOnce(self):
        scenarios = self.SMEMSim.get_scenarios()
        for s in scenarios:
            self.SMEMSim.scenario_start(s)
            self.SMEMSim.scenario_stop(s)

        self.assertEqual(len(self.SMEMSim.get_runing_scenarios()),
                         0, "Running scenario is not 0!")
        for region, alloc_info in self.memory.get_alloc_infos():
            self.assertEqual(len(alloc_info), 1, "SMEM region{} alloc is not clear!\nalloc_info {}".format(region, alloc_info))

    # DTV_ISDBTFHD -> HDMI -> DTV_MBC
    def test_HoldResource(self):
        self.SMEMSim.scenario_start(scenarios[1])
        self.SMEMSim.scenario_start(scenarios[13])
        self.SMEMSim.scenario_start(scenarios[0])

    def test_Random(self):
        for i in range(0, self.max_turn):
            logging.info("*** Ramdom #%d", i)

            runing = self.SMEMSim.get_runing_scenarios()
            notruning = list(set(self.SMEMSim.get_scenarios()) -
                             set(self.SMEMSim.get_runing_scenarios()))

            if(len(runing) == 0):
                isStart = True
            elif(len(notruning) == 0):
                isStart = False
            else:
                isStart = random.randint(0, 1) == 1

            if(isStart):
                self.SMEMSim.scenario_start((random.choice(notruning)))
            else:
                self.SMEMSim.scenario_stop(
                    (random.choice(self.SMEMSim.get_runing_scenarios())))

            for total in self.SMEMSim.get_total_alloc_size():
                if total[0] in self.alloc_max:
                    if self.alloc_max[total[0]] < total[1]:
                        self.alloc_max[total[0]] = total[1]
                else:
                    self.alloc_max[total[0]] = 0

            self.turn += 1

    def test_Menual(self):

        while(True):
            scenarios = self.SMEMSim.get_scenarios()
            runing = self.SMEMSim.get_runing_scenarios()
            # notruning = list(set(scenarios) - set(runing))

            print "\n\n##Scenario List"
            for i in range(0, len(scenarios)):
                print "{:2} {:20}".format(i, scenarios[i].name),
                if (i % 5) == 4:
                    print ""
            print ""

            print "\n##Runnging List"
            for i in range(0, len(runing)):
                print "{:2} {:20}".format(i, runing[i].name),
                if (i % 5) == 4:
                    print ""
            print ""

            input = raw_input("##input scenario start/stop number: ").split()
            # print input

            op = ""
            num = 0
            if len(input) > 0:
                op = input[0]
            if len(input) > 1:
                num = int(input[1])

            logging.debug("OP:%s, Num:%d", op, num)

            if (op == "start") & (num < len(scenarios)):
                self.SMEMSim.scenario_start(scenarios[num])
            elif (op == "stop") & (num < len(scenarios)):
                self.SMEMSim.scenario_stop(scenarios[num])
            elif op == "quit":
                break
            else:
                continue

            for total in self.SMEMSim.get_total_alloc_size():
                if total[0] in self.alloc_max:
                    if self.alloc_max[total[0]] < total[1]:
                        self.alloc_max[total[0]] = total[1]
                else:
                    self.alloc_max[total[0]] = 0

            self.turn += 1


# logging.basicConfig(level=logging.CRITICAL)
#logging.basicConfig(filename='/sys/kernel/debug/tracing/trace_marker', level=logging.ERROR)
#logging.basicConfig(level=logging.ERROR)
logging.basicConfig(level=logging.INFO)
# logging.basicConfig(level=logging.DEBUG)
# unittest.main()

if __name__ == "__main__":
    TS = unittest.TestSuite()

    # TS.addTest(SMEMSimTest("test_AllScenarioOnce"))
    # TS.addTest(SMEMSimTest("test_HoldResource"))
    TS.addTest(SMEMSimTest("test_Random"))
    # TS.addTest(SMEMSimTest("test_Menual"))

    runner = unittest.TextTestRunner()
    runner.run(TS)

