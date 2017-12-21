/* Manage TLS descriptors.  ARM version.
   Copyright (C) 2005-2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include <ldsodefs.h>
#include <tls.h>
#include <dl-tlsdesc.h>
#include <dl-unmap-segments.h>
#define _dl_tlsdesc_resolve_hold 0
#include <tlsdeschtab.h>

/* Unmap the dynamic object, but also release its TLS descriptor table
   if there is one.  */

void
_dl_unmap (struct link_map *map)
{
  _dl_unmap_segments (map);

#ifdef SHARED
  /* _dl_unmap is only called for dlopen()ed libraries, for which
     calling free() is safe, or before we've completed the initial
     relocation, in which case calling free() is probably pointless,
     but still safe.  */
  if (map->l_mach.tlsdesc_table)
    htab_delete (map->l_mach.tlsdesc_table);
#endif
}
