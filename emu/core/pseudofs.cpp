//----------------------------------------------------------------------------
//
// File:        pseudofs.cpp
// Date:        05-Sep-2003
// Programmer:  Marc Rousseau
//
// Description: A class to simulate a TI disk filesystem for TIFILES & FIAD files
//
// Copyright (c) 2003 Marc Rousseau, All Rights Reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
//
// Revision History:
//
//----------------------------------------------------------------------------

#include <unistd.h>

#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/common.hpp"
#include "../include/logger.hpp"
#include "../include/diskio.hpp"
#include "../include/pseudofs.hpp"
#include "../include/fileio.hpp"
#include "../include/support.hpp"

DBG_REGISTER ( __FILE__ );

static inline USHORT GetUSHORT ( const void *_ptr )
{
    FUNCTION_ENTRY ( NULL, "GetUSHORT", true );

    const UCHAR *ptr = ( const UCHAR * ) _ptr;
    return ( USHORT ) (( ptr [0] << 8 ) | ptr [1] );
}

static inline USHORT GetUSHORT_LE ( const void *_ptr )
{
    FUNCTION_ENTRY ( NULL, "GetUSHORT_LE", true );

    const UCHAR *ptr = ( const UCHAR * ) _ptr;
    return ( USHORT ) (( ptr [1] << 8 ) | ptr [0] );
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::Open
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
cPseudoFileSystem *cPseudoFileSystem::Open ( const char *filename )
{
    FUNCTION_ENTRY ( NULL, "cPseudoFileSystem::Open", true );
    
    cPseudoFileSystem *disk = new cPseudoFileSystem ( filename );

    if ( disk->IsValid () == false ) {
        disk->Release ( NULL );
        return NULL;
    }

    return disk;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::cPseudoFileSystem
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
cPseudoFileSystem::cPseudoFileSystem ( const char *filename ) :
    m_PathName ( NULL ),
    m_FileName ( NULL ),
    m_FileBuffer ( NULL ),
    m_File ( NULL ),
    m_CurrentSector ( NULL )
{    
    FUNCTION_ENTRY ( this, "cPseudoFileSystem ctor", true );

    // Store the entire path to the file
    m_PathName = new char [ strlen ( filename ) + 1 ];
    strcpy ( m_PathName, filename );

    // Find the filename (without any path)
    const char *ptr = strrchr ( filename, FILE_SEPERATOR );
    ptr = ( ptr != 0 ) ? ptr + 1 : filename;

    m_FileName = new char [ strlen ( ptr ) + 1 ];
    strcpy ( m_FileName, ptr );

    memset ( &m_FDR, 0, sizeof ( m_FDR ));

    m_CurrentSector = new sSector;
    memset ( m_CurrentSector, 0, sizeof ( sSector ));
    m_CurrentSector->Size = DEFAULT_SECTOR_SIZE;
   
    m_File = fopen ( filename, "rb" );

    if ( FindHeader () == false ) {
        fclose ( m_File );
        m_File = NULL;
    } else {
        LoadFileBuffer ();
    }
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::~cPseudoFileSystem
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
cPseudoFileSystem::~cPseudoFileSystem ()
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem dtor", true );

    if ( m_File != NULL ) {
	fclose ( m_File );
	m_File = NULL;
    }

    delete [] m_PathName;
    delete [] m_FileName;
    delete [] m_FileBuffer;
    delete m_CurrentSector;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::FindHeader
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::FindHeader ()
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::FindHeader", true );

    // See if there is either a TIFILES header or a file descriptor record (FIAD)
    char buffer [128];
    fseek ( m_File, 0, SEEK_SET );
    fread ( buffer, 1, sizeof ( buffer ), m_File );

    // Look for TIFILES (and all it's variants)
    if ( ConstructFDR_TIFILES ( buffer ) == false ) {
        // See if we can detect the FIAD header
        return ConstructFDR_FIAD ( buffer );
    }

    return true;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::ConstructFDR_TIFILES
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::ConstructFDR_TIFILES ( char *buffer )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::ConstructFDR_TIFILES", true );

    sTIFILES_Header *hdr = ( sTIFILES_Header * ) buffer;

    if (( hdr->length != 7 ) || ( strncmp ( hdr->Name, "TIFILES", 7 ) != 0 )) {
	return false;
    }

    m_FDR.FileStatus       = hdr->Status;
    m_FDR.RecordsPerSector = hdr->RecordsPerSector;
    m_FDR.TotalSectors     = hdr->SectorCount;
    m_FDR.EOF_Offset       = hdr->EOF_Offset;
    m_FDR.RecordLength     = hdr->RecordSize;
    m_FDR.NoFixedRecords   = hdr->RecordCount;

    // Do a quick sanity check (there seem to be two different ways to store RecordCount)
    int totalSectors = GetUSHORT ( &m_FDR.TotalSectors );
    int recCount = GetUSHORT_LE ( &m_FDR.NoFixedRecords );
    int rps = m_FDR.RecordsPerSector;

    // Make sure things match up - if not, swap bytes
    if (( recCount < ( totalSectors - 1 ) * rps ) || ( recCount > totalSectors * rps )) {
        swab (( char * ) &hdr->RecordCount, ( char * ) &m_FDR.NoFixedRecords, 2 );
        recCount = GetUSHORT_LE ( &m_FDR.NoFixedRecords );
        // If the byte swap didn't correct the problem then this probably isn't a TIFILES file
        if (( recCount < ( totalSectors - 1 ) * rps ) || ( recCount > totalSectors * rps )) {
            return false;
        }
    }

    // Try to find a filename in the header
    if ( IsValidName ( buffer + 16 ) == true ) {
	strncpy ( m_FDR.FileName, buffer + 16, MAX_FILENAME );
    } else if ( IsValidName ( buffer + 26 ) == true ) {
	strncpy ( m_FDR.FileName, buffer + 26, MAX_FILENAME );
    } else {
	// Just use the first 10 characters of the real filename
	char *ptr = strrchr ( m_FileName, FILE_SEPERATOR );
	ptr = ( ptr != NULL ) ? ptr + 1 : m_FileName;
	memset ( m_FDR.FileName, ' ', MAX_FILENAME );
	for ( int i = 0; i < MAX_FILENAME; i++ ) {
	    m_FDR.FileName [i] = toupper ( *ptr++ );
	    if (( *ptr == '\0' ) || ( *ptr == '.' )) break;
	}
    }

    return true;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::ConstructFDR_FIAD
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::ConstructFDR_FIAD ( char *buffer )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::ConstructFDR_FIAD", true );

    // Assume the file starts with an sFileDescriptorRecord
    sFileDescriptorRecord *fdr = ( sFileDescriptorRecord * ) buffer;

    // Make sure the contents of the FDR look reasonable
    if ( IsValidFDR ( fdr ) == false ) return false;

    // See if the file size is within the size indicated by the 'FDR'
    int totalSectors = GetUSHORT ( &fdr->TotalSectors );
    long expectedMin = ( totalSectors - 1 ) * DEFAULT_SECTOR_SIZE + fdr->EOF_Offset;
    long expectedMax = totalSectors * DEFAULT_SECTOR_SIZE;

    fseek ( m_File, 0, SEEK_END );
    long actualSize = ftell ( m_File ) - 128;

    if (( actualSize < expectedMin ) || ( actualSize > expectedMax )) return false;

    // We made it this far - it's most likely an FIAD file
    memset ( &m_FDR, 0, sizeof ( m_FDR ));
    memcpy ( &m_FDR, buffer, 128 );

    return true;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::LoadFileBuffer
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
void cPseudoFileSystem::LoadFileBuffer ()
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::LoadFileBuffer", true );

    int totalSectors = GetUSHORT ( &m_FDR.TotalSectors );
    size_t maxSize = totalSectors * DEFAULT_SECTOR_SIZE;
    m_FileBuffer = new UCHAR [ maxSize ];

    fseek ( m_File, 128, SEEK_SET );
    
    if ( fread ( m_FileBuffer, 1, maxSize, m_File ) != maxSize ) {
        ERROR ( "Failed to read " << maxSize << " bytes from file" );
    }
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::FileCount
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
int cPseudoFileSystem::FileCount () const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::FileCount", true );

    return 1;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetFileDescriptor
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
const sFileDescriptorRecord *cPseudoFileSystem::GetFileDescriptor ( int index ) const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetFileDescriptor", true );

    ASSERT ( index == 0 );

    return &m_FDR;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetFreeSectors
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
int cPseudoFileSystem::FreeSectors () const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetFreeSectors", true );

    return 0;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetTotalSectors
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
int cPseudoFileSystem::TotalSectors () const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetTotalSectors", true );

    return GetUSHORT ( &m_FDR.TotalSectors );
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetFileSector
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
sSector *cPseudoFileSystem::GetFileSector ( sFileDescriptorRecord *fdr, int index )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetFileSector", true );

    if ( fdr != &m_FDR ) return NULL;

    m_CurrentSector->Data = m_FileBuffer + index * DEFAULT_SECTOR_SIZE;

    return m_CurrentSector;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::ExtendFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
int cPseudoFileSystem::ExtendFile ( sFileDescriptorRecord *fdr, int )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::ExtendFile", true );

    if ( fdr != &m_FDR ) return -1;

    FATAL ( "Function not implemented" );

    return -1;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::TruncateFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
void cPseudoFileSystem::TruncateFile ( sFileDescriptorRecord *fdr, int )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::TruncateFile", true );

    if ( fdr != &m_FDR ) return;

    FATAL ( "Function not implemented" );
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::DiskModified
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
void cPseudoFileSystem::DiskModified ()
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::DiskModified", true );

    // Write m_CurrentSector to the file

    FATAL ( "Function not implemented" );
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetPath
// Purpose:     Return the filename of the disk
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::GetPath ( char *buffer, size_t maxLen ) const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetPath", true );

    if ( maxLen < strlen ( m_PathName ) + 1 ) {
	ERROR ( "Destination buffer is too short" );
        return false;
    }

    strcpy ( buffer, m_PathName );

    return true;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::GetName
// Purpose:     Return the filename of the disk
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::GetName ( char *buffer, size_t maxLen ) const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::GetName", true );

    if ( maxLen < strlen ( m_FDR.FileName ) + 1 ) {
	ERROR ( "Destination buffer is too short" );
        return false;
    }

    strcpy ( buffer, m_FDR.FileName );

    return true;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::IsValid
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::IsValid () const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::IsValid", true );

    return ( m_File != NULL ) ? true : false;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::IsCollection
// Purpose:     Return the filename of the disk
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::IsCollection () const
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::IsCollection", true );

    return false;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::OpenFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
cFile *cPseudoFileSystem::OpenFile ( const char * )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::OpenFile", true );

    return cFileSystem::CreateFile ( &m_FDR );
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::CreateFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
cFile *cPseudoFileSystem::CreateFile ( const char *, UCHAR, int )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::CreateFile", true );

    FATAL ( "Function not implemented" );

    return NULL;

}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::AddFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::AddFile ( cFile * )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::AddFile", true );

    FATAL ( "Function not implemented" );

    return false;
}

//------------------------------------------------------------------------------
// Procedure:   cPseudoFileSystem::DeleteFile
// Purpose:
// Parameters:
// Returns:
// Notes:
//------------------------------------------------------------------------------
bool cPseudoFileSystem::DeleteFile ( const char * )
{
    FUNCTION_ENTRY ( this, "cPseudoFileSystem::DeleteFile", true );

    FATAL ( "Function not implemented" );

    return false;
}
