/*
 * defrag.cc - Operations on relevant file defragmentation
 *
 * Copyright (C) 2011 by Andreas Rid
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "defrag.hh"
#include "balloc.h"
#include "fiemap.hh"
#include "logging.hh"
#include "config.hh"
#include "buddycache.hh"

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <mntent.h>

/* opendir*/
#include <sys/types.h>
#include <dirent.h>
#include <stdexcept>

#include <boost/foreach.hpp>

// set thread priority
#include <linux/unistd.h>
#include <sys/resource.h>

/*
 * Create a temporary file in directory dir with size in bytes.
 * On success return the new file path
 * otherwise throw std::runtime_error
 */
std::string createTempFile(fs::path dir, __u64 size = 0)
{
    int fd;
    char cpath[PATH_MAX];
    std::string path;
    
    path = (dir / PROGRAM_NAME"-XXXXXX").string();
    strncpy(cpath, path.c_str(), PATH_MAX);

    fd = mkostemp(cpath, O_RDWR |O_CREAT);
    if(-1 == fd)
        throw std::runtime_error(std::string("Cannot create donor file:")
                                 + path + strerror(errno));

    if(size)
        if (fallocate(fd, 0, 0, size) < 0)
        {
            close(fd);
            throw std::runtime_error(std::string("fallocated failed: ")
                                     + cpath + strerror(errno));
        }
    
    close(fd);
    
    return cpath;
}

std::string createTempDir(fs::path dir)
{
    std::string path_template = (dir / PROGRAM_NAME"-XXXXXX").string();
    return mkdtemp((char*)path_template.c_str());
}

/*
 * Move temporary a file to another directory.
 * To avoid race condition an hard link is created to the original file.
 * This cause that the original filename will be changed.
 *
 * The function returns the new file path.
 */
std::string renameTempFile(fs::path orig, fs::path dir)
{
    std::string path;

    do
    {
        path = createTempFile(dir, 0);
        unlink(path.c_str());
            
        /*
         * If someone created a file with the same name in the mean time,
         * link will fail.
         */
    }
    while(0 != link(orig.string().c_str(), path.c_str()));
    
    unlink(orig.string().c_str());

    return path;
}

Defrag::Defrag()
{
    invalid_file_type = 0;
    not_writable      = 0;
    not_extent_based  = 0;
    empty_files       = 0;
}
Optimizer::Optimizer()
{
}

/*
 * Check weather your Linux Kernel supports pre-allocation ioctl on device.
 * This function will fail if filesystem is not an ext4 or your kernel does
 * not support pre-allocation ioctl.
 *
 * Don't worry. It's just a dummy call. The filesystem remains unaffected.
 *
 * Return: true  on success
 *         otherwise  false
 */
bool Defrag::doesKernelSupportPA(Device device)
{
    int ret = true;
    int fd;
    
    fd = open(device.getMountPoint().string().c_str(), O_RDONLY);
    if(0 > fd)
    {
        error("Cannot open mount point: %s: %s", 
                device.getMountPoint().string().c_str(), strerror(errno));
        return false;
    }
    
    struct ext4_prealloc_list* pa_list = (struct ext4_prealloc_list*)
    malloc(sizeof(struct ext4_prealloc_list)+sizeof(ext4_prealloc_info));
    pa_list->pl_count = 1;
    
    if(0 > ioctl(fd, EXT4_IOC_GET_PA, pa_list))
        if(errno == ENOTTY)
            ret = false;

    close(fd);

    return ret;
}


bool checkFileSystem(Device& device)
{
    if(device.getFileSystem() != "ext4")
    {
        std::string dev_name = device.getDevicePath();
        if(dev_name.at(0) != '/')
            dev_name = device.getMountPoint().string();
        info("%s is not an ext4 filesystem.", dev_name.c_str());
        return false;
    }

    if(!device.open())
    {
        info("Couldn't find valid filesystem superblock on %s.",
             device.getDevicePath().c_str());
        return false;
    }

    if(!device.hasExtentFeature())
    {
        info("Ext4 filesystem on %s has not extent feature enabled.",
             device.getDevicePath().c_str());
        return false;
    }
    
    return true;
}


/*
 * This is the main-entry-point for relevant file defragmentation.
 */
void Optimizer::relatedFiles(std::vector<fs::path>& files)
{
    try {
        typedef std::pair<dev_t, std::vector<fs::path> >       files_per_dev_pair_t;
        typedef std::pair<Device, std::vector<OrigDonorPair> > filemap_pair_t;
        
        std::map<dev_t, std::vector<fs::path> > files_per_dev;
        std::map<Device, std::vector<OrigDonorPair> > filemap;
        
        int files_unavailable     = 0;
        int wrong_filesystem_type = 0;

        /*
         * Sort files per devices
         */
        BOOST_FOREACH(fs::path& file, files)
        {
            struct stat st;
            if(-1 == stat(file.string().c_str(), &st))
            {
                info("Cannot open file: %s: %s", file.string().c_str(), strerror(errno));
                files_unavailable++;
            }
            else
                files_per_dev[st.st_dev].push_back(file);
        }

        /*
         * Check file attributes
         */
        BOOST_FOREACH(files_per_dev_pair_t dev_it, files_per_dev)
        {
            Device device(dev_it.first);
            if(checkFileSystem(device))
                filemap.insert(filemap_pair_t(device, checkFilesAttributes(dev_it.second)));
            else
                wrong_filesystem_type += dev_it.second.size();
        }
        
        /*
         * Display file statistics 
         */
        if(files_unavailable)
            notice("%*d/%d file(s) are not available",
                   (int)(log10(files.size())+1), files_unavailable, files.size());
        if(wrong_filesystem_type)
            notice("%*d/%d file(s) not on an ext4 filesystem",
                   (int)(log10(files.size())+1), wrong_filesystem_type, files.size());
        if(invalid_file_type)
            notice("%*d/%d file(s) have invalid file type.",
                   (int)(log10(files.size())+1), invalid_file_type , files.size());
        if(not_writable)
            notice("%*d/%d file(s) are presently not writable.",
                   (int)(log10(files.size())+1), not_writable , files.size());
        if(not_extent_based)
            notice("%*d/%d file(s) cannot set inode extent flag.",
                   (int)(log10(files.size())+1), not_extent_based , files.size());
        if(empty_files)
            notice("%*d/%d file(s) have no blocks.",
                   (int)(log10(files.size())+1), empty_files , files.size());
        
        if(filemap.empty())
            return;
        
        /*
         * Apply defrag mode
         */
        if("auto" == Config::get<std::string>("defrag_mode"))
        {
            if(doesKernelSupportPA(filemap.begin()->first))
                Config::set<std::string>("defrag_mode", "pa");
            else
                Config::set<std::string>("defrag_mode", "locality_group");
        }
        else if("pa" == Config::get<std::string>("defrag_mode"))
            if(!doesKernelSupportPA(filemap.begin()->first))
                throw std::logic_error("Kernel does not support pre-allocation");
        
        std::string mode = Config::get<std::string>("defrag_mode");
        if(mode == "pa")
            mode = "pre-allocation";
        else if(mode == "locality_group")
            mode = "locality group";
        else if(mode == "tld")
            mode = "top level directory";
        else
            throw std::runtime_error(std::string("Unknown defrag mode: ") + mode);
        
        notice("Defrag mode: %s", mode.c_str());
        
        /*
         * Let's rock!
         */
        BOOST_FOREACH(filemap_pair_t map, filemap)
        {
            notice("Processing %d file(s) on device %s (mount-point: %s)",
                   map.second.size(),
                   map.first.getDevicePath().c_str(),
                   map.first.getMountPoint().string().c_str());
            
            defragRelatedFiles(map.first, map.second);
        }
    }
    catch(UserInterrupt& e)
    {
    }
}


/*
 * Check file's attributes.
 * Use this function to sort out all files move extent call will fail.
 * If file is invalid std::runtime_error is thrown,
 * otherwise return file's block count
 */
std::vector<OrigDonorPair> Defrag::checkFilesAttributes(std::vector<fs::path>& i_files)
{
    struct stat st;
    int flags;
    OrigDonorPair odp;
    
    std::vector<OrigDonorPair> checked_files;

    BOOST_FOREACH(fs::path& file, i_files)
    {
        const char* path = file.string().c_str();
        
        // we cannot open fd of symbolic link. therefore open with O_NOFOLLOW
        // in case of a symbolic link will fail regardless
#ifdef MOVE_EXT_RDONLY_FLAG
        int fd = open(path, O_RDONLY | O_NOFOLLOW);
#else
        int fd = open(path, O_RDWR | O_NOFOLLOW);
#endif
        if (fd < 0)
        {
            switch(errno)
            {
                case ELOOP:
                    invalid_file_type++;
                    info("Cannot open file: %s: Is a symbolic link.", path);
                    continue;
                case EISDIR:
                    invalid_file_type++; break;
                default:
                    not_writable++;
            }

            info("Cannot open file: %s: %s", path, strerror(errno));
            continue;
        }

        if(fstat(fd, &st) < 0)
        {
            close(fd);
            info("Cannot get file statistics: %s: %s",path,strerror(errno));
            invalid_file_type++;
            goto cont;
        }

        if(!S_ISREG(st.st_mode))
        {
            info("%s is not a regular file.", path);
            invalid_file_type++;
            goto cont;
        }

        if(0 > ioctl(fd, FS_IOC_GETFLAGS, &flags))
        {
            info("Cannot receive inode flags: ", path, strerror(errno));
            invalid_file_type++;
            goto cont;
        }

        if(!(flags & EXT4_EXTENTS_FL))
        {
            flags |= EXT4_EXTENTS_FL;
            if(0> ioctl(fd, FS_IOC_SETFLAGS, &flags))
            {
                info("Cannot convert file %s to be extent based: %s",
                     path, strerror(errno));
                not_extent_based++;
                goto cont;
            }
        }

        if(flags & FS_IMMUTABLE_FL)
        {
            info("%s is immutable.", path);
            not_writable++;
            goto cont;
        }

        odp.blocks= get_block_count(fd);

        if(0 == odp.blocks)
        {
            info("File %s has no blocks.", path);
            empty_files++;
            goto cont;
        }
        odp.origPath = file;
        checked_files.push_back(odp);
cont:
        close(fd);
    }


    
    return checked_files;
}

/*
 * Search for free block range by calling pre-allocation ioctl.
 *
 * Algorithm:
 *   - Create a temporary file.
 *   - Preallocate requested blocks
 *   - Parse block range of and select the biggest extent
 *   - Discard all pre-allocations
 *
 * Return the biggest found extent.
 */
Extent findExtent(Device device, __u64 phint, __u32 len)
{
    Extent extent;

    /*
     * create temporary file
     */
    char cfiletemplate[PATH_MAX];
    std::string file_template = (device.getMountPoint() / PROGRAM_NAME"-XXXXXX").string();
    strncpy(cfiletemplate, file_template.c_str(), PATH_MAX);
    int fd = mkostemp(cfiletemplate, O_RDWR |O_CREAT);
    if(fd < 0)
        throw std::runtime_error("Cannot create a temporary file");

    unlink(cfiletemplate);

    /*
     * Pre-allocte somewhere a free block range
     */
    struct ext4_prealloc_info pi;
    memset(&pi, 0, sizeof(ext4_prealloc_info));

    pi.pi_pstart = phint;
    pi.pi_lstart = 0;
    
    //cause pa ioctl is limited len must no larger than total blocks - 10
    pi.pi_len = std::min(device.getBlocksPerGroup() - 10, len);
    pi.pi_flags = EXT4_MB_ADVISORY;

    if(-1 == ioctl(fd, EXT4_IOC_CONTROL_PA, &pi))
    {
        if(errno == ENOTTY)
            throw std::runtime_error(
                    "ioctl EXT4_IOC_CONTROL_PA not supported");
        else
            throw std::runtime_error("Out of disk space");
    }

    /*
     * Receive block allocations
     */
#define PA_INFO_CNT 10
    struct ext4_prealloc_list* pa_list =
            (struct ext4_prealloc_list*) malloc(
                    sizeof(struct ext4_prealloc_list)+PA_INFO_CNT
                    *sizeof(ext4_prealloc_info));
    pa_list->pl_count = PA_INFO_CNT;
    
    if(0 > ioctl(fd, EXT4_IOC_GET_PA, pa_list))
        throw std::runtime_error(
                    std::string("EXT4_IOC_GET_PA failed") + strerror(errno));

    for(__u32 i = 0; i < pa_list->pl_entries; i++)
    {
        if(extent.len < pa_list->pl_space[i].pi_len)
        {
            extent.start = pa_list->pl_space[i].pi_pstart;
            extent.len   = pa_list->pl_space[i].pi_len;
        }
        debug("Found extent: %llu:%u",
                pa_list->pl_space[i].pi_pstart, pa_list->pl_space[i].pi_len);
    }

    /*
     * Discard pre-allocations
     */
    pi.pi_flags = EXT4_MB_DISCARD_PA;
    if( ioctl(fd, EXT4_IOC_CONTROL_PA, &pi) < 0)
        throw std::runtime_error("Cannot discard pre-allocation");

    close(fd);
    
    return extent;
}

/*
 * Find free block range on device. phint says where to start searching.
 * user space pre-allocation is limited to blocks_per_group -10.
 * Use buddy information to search for bigger block range.
 */
Extent findFreeSpace(Device device, __u64 phint, __u64 len)
{
    // check for pre-allocation block request limit
    // use BuddyCache to satisfy the request
    if( len > device.getBlocksPerGroup() - 10)
    {
        BuddyCache buddyCache(device);

        // find empty flex group
        if(len > device.getBlocksPerGroup())
        {
            int flex = buddyCache.findEmptyFlex();
            if(flex > 0)
            {
                return Extent(
                        flex<<device.getLogGroupsPerFlex()
                        * device.getBlocksPerGroup(),
                        device.freeBlocksPerFlex());
            }
        }

        // no empty flex group found
        // search for empty block groups
        int bg = buddyCache.findEmptyGroup();
        if(bg > 0)
        {
            return Extent(
                bg*device.getBlocksPerGroup() + buddyCache.at(bg).first,
                        buddyCache.at(bg).free);
        }
    }

    return findExtent(device, phint, len);
}

/*
 * Creating donor files using pre-allocation patch from Kazuya Mio.
 */
void Defrag::createDonorFiles_PA(Device& device, 
                                 std::vector<OrigDonorPair>& files)
{
    int fd;
    /*
     * calculate total amount of blocks
     */
    size_t blk_count = 0;
    BOOST_FOREACH(OrigDonorPair& odp, files)
        blk_count += odp.blocks;


    Extent free_space = findFreeSpace(device, 0, blk_count);
    BOOST_FOREACH(OrigDonorPair& odp, files)
    {
        odp.donorPath = createTempFile(device.getMountPoint(), 0);
        fd = open(odp.donorPath.string().c_str(), O_WRONLY, 0700);
        if(0 > fd)
            throw std::runtime_error(std::string("Cannot open donor file: ")
                                 + odp.donorPath.string() + strerror(errno));

        __u64 file_offset = 0;
        do
        {
            if(free_space.len == 0)
                free_space = findFreeSpace(device, 0, blk_count);

            __u64 pa_blocks = std::min( odp.blocks - file_offset,
                                             (__u64)free_space.len);
        
            try {
                device.preallocate(fd,
                            free_space.start,
                            file_offset,
                            pa_blocks,
                            EXT4_MB_MANDATORY);
                
                file_offset      += pa_blocks;
                blk_count        -= pa_blocks;
                free_space.len   -= pa_blocks;
                free_space.start += pa_blocks;
            }
            catch(Extent& e)
            {
                free_space = e;
            }
    
        } while(file_offset < odp.blocks);

        if (fallocate(fd, 0, 0, odp.blocks * device.getBlockSize()) < 0)
            throw std::runtime_error(
                    std::string("Cannot allocate blocks for donor: ")
                                + odp.donorPath.string() + strerror(errno));
        close(fd);
    }
}

/*
 * Creating donor files using locality group for small files.
 * All files are placed into an locality group. Therefore increase the limit
 * for small files.
 */
void Defrag::createDonorFiles_LocalityGroup(Device& device,
                                            std::vector<OrigDonorPair>& files)
{
    __s64 old_mb_stream_req = -1;
    __s64 old_mb_group_prealloc = -1;

    try {
        old_mb_stream_req     = device.getTuningParameter("mb_stream_req");
        old_mb_group_prealloc = device.getTuningParameter("mb_group_prealloc");

        /*
         * calc total amount of blocks and highest block count
         */
        __u64 total_blk_cnt = 0;
        __u64 highest_blk_cnt = 0;
        BOOST_FOREACH(OrigDonorPair& odp, files)
        {
            interruptionPoint();

            if(highest_blk_cnt < odp.blocks)
                highest_blk_cnt = odp.blocks;
                
            total_blk_cnt += odp.blocks;
        }

        /*
         * set size of new locality groups.
         * It does not make sense to set a larger size than free blocks can
         * exists per block group. Even though FLEX_BG feature is enabled
         * the multi-block allocator function ext4_ext_map_blocks() in the
         * Linux Kernel searches only per group not per flex.
         */
        device.setTuningParameter("mb_group_prealloc", 
                        std::min(total_blk_cnt, device.freeBlocksPerGroup()));

        /*
         * Increase block limit for small files. 
         * So all donor files are placed into the locality group.
         * Furthermore multi-block allocator does not normalize the block
         * request.
         */
        device.setTuningParameter("mb_stream_req", highest_blk_cnt + 1);

        /*
         * TODO: fill up previous locality group on an effective way
         */
        //fillUpLocalityGroup(device);
        
        /*
         * Allocating donor files in locality group
         */
        BOOST_FOREACH(OrigDonorPair& odp, files)
        {
            odp.donorPath = createTempFile(device.getMountPoint(),
                                           odp.blocks*device.getBlockSize());
        }

        /*
         * Reset tuning parameters
         */
        device.setTuningParameter("mb_stream_req", old_mb_stream_req);
        device.setTuningParameter("mb_group_prealloc", old_mb_group_prealloc);
    }
    
    catch(std::exception& e)
    {
        if(-1 != old_mb_stream_req)
            device.setTuningParameter("mb_stream_req", old_mb_stream_req);

        if(-1 != old_mb_group_prealloc)
            device.setTuningParameter("mb_group_prealloc",
                                      old_mb_group_prealloc);
        throw e;
    }
    catch(...)
    {
        throw std::logic_error(
                "Do not throw an object not derived from std::exception");
    }
}

/*
 * The Orlov directory algorithm spreads top level directories on disk. The 
 * inode of newly created top level directory is allocated in an empty or if 
 * no one exists mostly free block group. 
 * 
 * While there exists free blocks and free inodes all newly created donor 
 * files will be placed in the same block group as well. There is no guarantee 
 * for sequentially placement. Sometimes files are skewed. The main 
 * disadvantage however, is that each block allocation request is normalized. 
 * This means it is rounded up to a power of two. Small holes between each 
 * file are the result.

 * In addition small files will be placed in locality group instead of our tld.
 * Therefore while creating donor files set the mb_stream_req to 0.
 */
void Defrag::createDonorFiles_TLD(Device& device,
                                                std::vector<OrigDonorPair>& files)
{
    __s64 old_mb_stream_req = -1;
    fs::path tld;

    try {
    
        // create new top level directory
        tld = createTempDir(device.getMountPoint());

        // backup the original mb_stream_req value
       // and set the maximum file sizes for small smales to 0 blocks.
        old_mb_stream_req = device.getTuningParameter("mb_stream_req");
        device.setTuningParameter("mb_stream_req", 0);

        // create donor files
        BOOST_FOREACH(OrigDonorPair& odp, files)
        {
            interruptionPoint();
            
            try {
                std::string tmpFile = createTempFile(tld, 
                                           odp.blocks * device.getBlockSize());

                // Once we created all donor files we will remove our top level
                // directory again. Therefore move the file to the root directory
                odp.donorPath = renameTempFile(tmpFile, device.getMountPoint());
            }
            catch(std::runtime_error& e)
            {
                warn("%s", e.what());
            }
        }
        
        // Delete top level directory
        // The directory should be empty
        if(-1 == rmdir(tld.string().c_str()))
            error("cannot unlink base directory: %s: %s", 
                            tld.string().c_str(), strerror(errno));

        /*
         * reset tuning parameters
         */
        device.setTuningParameter("mb_stream_req", old_mb_stream_req);
        
    }
    
    // An error occurred!
    // Cleanup and restore system environment
    catch(std::exception& e)
    {
        if(!tld.empty())
            if(-1 == rmdir(tld.string().c_str()))
                if(errno != ENOENT)
                    error("cannot unlink base directory: %s: %s", 
                                        tld.string().c_str(), strerror(errno));
        if(old_mb_stream_req != -1)
            device.setTuningParameter("mb_stream_req", old_mb_stream_req);

        throw e;
    }
    catch(...)
    {
        throw std::logic_error("Do not throw an object not derived from std::exception");
    }
}

/*
 * Create for each original file an appropriate donor file.
 * Select one of the three mode for creating donor files.
 */
void Defrag::createDonorFiles(Device& device, std::vector<OrigDonorPair>& defragPair )
{
    /*
     * set high priority to current thread
     */
    pid_t tid;
    tid = syscall(__NR_gettid);
    int old_priority = getpriority(PRIO_PROCESS, tid);
    
    if(-1 == setpriority(PRIO_PROCESS, tid, -20))
        warn("Cannot set thread priority to -20: %s", strerror(errno));

    /*
     * choose mode
     */
    std::string mode = Config::get<std::string>("defrag_mode");

    if(mode == "pa")
        createDonorFiles_PA(device, defragPair);
    else if(mode == "tld")
        createDonorFiles_TLD(device, defragPair);
    else if(mode == "locality_group")
        createDonorFiles_LocalityGroup(device, defragPair);
    else
        throw std::logic_error(std::string("Unknown defrag mode: ") + mode);
    /*
     * reset priority
     */
    setpriority(PRIO_PROCESS, tid, old_priority);
    
}

void checkImprovement(Device& device, std::vector<OrigDonorPair>& files)
{
    int frag_cnt_donor = 0;
    int frag_cnt_orig = 0;
    
    int fd;
    struct fiemap* fmap;
    __u64 first_block = 0;
    __u64 prev_block  = 0;
    
    BOOST_FOREACH(OrigDonorPair& odp, files)
    {
        fd = open(odp.donorPath.string().c_str(), O_RDONLY);
        if(fd < 0)
            throw std::logic_error(std::string("cannot open file: ")+odp.donorPath.string() + ": " + strerror(errno));
        fmap = ioctl_fiemap(fd);

        for(__u32 i = 0; i< fmap->fm_mapped_extents; i++)
        {
            first_block = fmap->fm_extents[i].fe_physical>>12;
            if(abs(first_block - prev_block -1) > 31)
                frag_cnt_donor++;
            
            prev_block = first_block + (fmap->fm_extents[i].fe_length>>12) - 1;
        }
    }

    first_block = 0;
    prev_block  = 0;
    BOOST_FOREACH(OrigDonorPair& odp, files)
    {
        fd = open(odp.origPath.string().c_str(), O_RDONLY);
        if(fd < 0)
            throw std::logic_error(std::string("cannot open file: ")+odp.donorPath.string() + ": " + strerror(errno));
        fmap = ioctl_fiemap(fd);

        for(__u32 i = 0; i< fmap->fm_mapped_extents; i++)
        {
            first_block = fmap->fm_extents[i].fe_physical>>12;
            if(abs(first_block - prev_block -1) > 31)
                frag_cnt_orig++;
            
            prev_block = first_block + (fmap->fm_extents[i].fe_length>>12) - 1;
        }
    }

    notice("Total fragment count before/afterwards:  %d/%d", frag_cnt_orig, frag_cnt_donor);
    if(frag_cnt_donor >= frag_cnt_orig)
        throw std::runtime_error("There is no improvement possible.");
}
/*
 * Main algorithm of related file defragmentation.
 *
 * 1. Check file attributes:
         Sort all files out for all those, who move_ext ioctl will fail.
 * 2. Create all donor files
 *      Select one of the three modes of creating donor files
 * 3. Check improvement
 * 4. Open original and donor file
 * 5. Call move extent ioctl (EXT4_IOC_MOVE_EXT)
 * 6. fadvice to free donor file from page cache
 * 7. Delete donor file
 */ 
void Defrag::defragRelatedFiles(Device& device, std::vector<OrigDonorPair>& files)
{
    int fcnt = 0;
    int orig_fd;
    int donor_fd;

    try {

        createDonorFiles(device, files);

        checkImprovement(device, files);
            
        BOOST_FOREACH(OrigDonorPair& odp, files)
        {
            interruptionPoint();
            info("[ %*d/%d ] %*llu block(s)    %s", (int)(log10(files.size())+1), ++fcnt, 
                 files.size(), 
                 6, odp.blocks,
                 odp.origPath.string().c_str());
            
            /*
             * prepare original and donor file descriptors
             */
#ifdef MOVE_EXT_RDONLY_FLAG
            orig_fd = open(odp.origPath.string().c_str(), O_RDONLY | O_NOFOLLOW);
#else
            orig_fd = open(odp.origPath.string().c_str(), O_RDWR | O_NOFOLLOW);
#endif

            if(orig_fd < 0)
            {
                error("Cannot open orig file %s: %s", 
                      odp.origPath.string().c_str(), strerror(errno));
                continue;
            }
            
            donor_fd = open(odp.donorPath.string().c_str(), 
                            O_WRONLY | O_CREAT, 0700);
            if(donor_fd < 0)
            {
                error("Cannot open donor file %s: %s", 
                      odp.donorPath.string().c_str(), strerror(errno));
                continue;
            }

            try {
                device.moveExtent(orig_fd, donor_fd, 0, odp.blocks);
            }
            catch(std::exception& e)
            {
                error("%s", e.what());
            }
            
            if(posix_fadvise(orig_fd, 0, odp.blocks*device.getBlockSize(),
                            POSIX_FADV_DONTNEED))
            {
                warn("fadvice failed: %s", strerror(errno));
            }
        
            if (unlink(odp.donorPath.string().c_str()) < 0)
                error("Cannot unlink donor fd: %s", strerror(errno));
            odp.donorPath.clear();
        
            close(orig_fd);
            close(donor_fd);
        }
    }
    
    catch(std::exception& e)
    {
        BOOST_FOREACH(OrigDonorPair& odp, files)
            if(-1 == remove(odp.donorPath.string().c_str()))
                if(errno != ENOENT)
                    error("Cannot remove donor file: %s: %s",
                             odp.donorPath.string().c_str(), strerror(errno));
        error("%s", e.what());
    }
}
