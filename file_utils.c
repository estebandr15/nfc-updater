#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

unsigned char *find_bin_file()
{
    DIR *dir;
    FILE *f;     // Puntero al fichero
    long f_size; // Tamaño del fichero
    struct dirent *entry;
    static char bin_file[256];
    unsigned char *firmware_buffer; // Puntero para el buffer

    if ((dir = opendir(".")) == NULL)
    {
        perror("No se puede abrir directorio...");
        return NULL;
    }

    while ((entry = readdir(dir)) != NULL) // Recorre los archivos del directorio
    {
        if (strstr(entry->d_name, ".bin") != NULL) // Buscar archivo .bin
        {
            snprintf(bin_file, sizeof(bin_file), "%s/%s", ".", entry->d_name); // Construir ruta completa
            closedir(dir);                                                           // Cerrar directorio
            return bin_file;
        }
    }
    closedir(dir); // Cerrar directorio si no se encuentra
    return NULL;   // Devolver NULL si no se encuentra
}
