/*
 * eventcatcher.cc - Handle audit event from Listener
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

#include "eventcatcher.hh"
#include "listener.hh"
#include "logging.hh"

#include <boost/foreach.hpp>

void ScanFsAccess::insert(FilePtr& f)
{
    list.push_back(f);
}

std::deque<FilePtr> ScanFsAccess::getFileList()
{
    std::deque<FilePtr> ret;
    BOOST_FOREACH(FilePtr f, list)
        if(f.isValid())
            ret.push_back(f);
    
    return ret;
}

void ScanFsAccess::observeApp(std::string comm)
{
    // name of process is limited to 16 characters
    if(comm.size() > 15)
        comm.resize(15);
    observe_apps.insert(comm);
}

fs::path ScanFsAccess::readLink(fs::path& path)
{
    char lnk_buf[PATH_MAX];
    memset(&lnk_buf, '\0', PATH_MAX);
    if(0 > readlink(path.string().c_str(), (char*)&lnk_buf, PATH_MAX))
        throw "";
    return realpath(lnk_buf, path.parent_path());
}

fs::path ScanFsAccess::getPath2RegularFile(fs::path& path)
{
    fs::path linkTo = path;
    try {
        while(1)
            linkTo = readLink(linkTo);
    }
    catch(...)
    {}
    return linkTo;
}
    
void ScanFsAccess::handleAuditEvent(boost::shared_ptr<AuditEvent> event)
{
    // Since Linux set audit filter AUDT_FILTER_ENTRY as deprecated, there is no way
    // to monitor exit() syscall events.
    // Cause we identify processes by its process id, we have to check whether 
    // a process exists and another process got the same process id like the one
    // watched before.
    if(event->type == Execve)
    {
        std::set<pid_t>::iterator it;
        it = observe_pids.find(event->pid);
        if(it != observe_pids.end())
            observe_pids.erase(it);
    }

    // does this event apply to a process name we are watching?
    if(!observe_apps.empty())
    {
        if(observe_apps.find(event->comm) != observe_apps.end())
            observe_pids.insert(event->pid);


        // follow process id's
        if(observe_pids.find(event->pid) == observe_pids.end())
        {
            if(observe_pids.find(event->ppid) == observe_pids.end())
                return;
            else
                observe_pids.insert(event->pid);
        }
    }
    debug("syscall: %d RO: %d", event->type, event->readOnly);
    
    switch(event->type)
    {
        case Creat:
        case Truncate:
        {
            FilePtr file = FilePtr(event->dev, event->ino, event->path);
            if(file.unique() || file.isValid())
            {
                info("File was modified: \t%s", 
                                  event->path.string().c_str());
                file.setInvalid();
            }
            if(file.unique())
                insert(file);
        }
            break;

        case Open:
        case OpenAt:
            if(!event->readOnly)
            {
                FilePtr file = FilePtr(event->dev, event->ino, event->path);
                if(file.unique() || file.isValid())
                {
                    info("Opened writable: \t%s", 
                                    event->path.string().c_str());
                    file.setInvalid();
                }
                if(file.unique())
                    insert(file);

                break;
            }

        case Execve:
            if(event->type == Execve)
            {
                FilePtr file = FilePtr(event->exe);
                if(file.unique())
                {
                    info("Insert executable: \t%s", event->exe.string().c_str());
                    insert(file);
                }
            }
            
        default:
        {
            /*
             * ignore non regular files
             */
            struct stat st;
            if(0 > stat(event->path.string().c_str(), &st))
            {
                error("stat: %s: %s", event->path.string().c_str(), strerror(errno));
                return;
            }
            
            // sometimes linux audit sends wrong inode and dev numbers
            if(st.st_ino != event->ino)
            {
                debug("Inode Number differ! %s i_event: %d, d_event: %d - i_real: %d, d_real: %d", event->path.string().c_str(), event->ino, event->dev, st.st_ino, st.st_dev);
                return;
            }
            
            if(!S_ISREG(st.st_mode))
            {
                const char* type;
                
                if(S_ISDIR(st.st_mode))
                    type = "directory";
                else if(S_ISLNK(st.st_mode))
                    type = "soft link";
                else if(S_ISCHR(st.st_mode))
                    type = "character device";
                else if(S_ISBLK(st.st_mode))
                    type = "block device";
                else
                    type = "unknown file type";
                
                info("Refuse %s: \t%s", type, event->path.string().c_str());
                break;
            }
            FilePtr file;
            file = FilePtr(event->dev, event->ino, getPath2RegularFile(event->path));
            if(file.unique())
            {
                info("Insert regular file: \t%s", file.getPath().string().c_str());
                insert(file);
            }
        }
    }
}
