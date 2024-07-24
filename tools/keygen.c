#include <getopt.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

int write_key(char *path, unsigned char *key, size_t len) {
  FILE *f;
  size_t written;

  f = fopen(path, "w");

  if (f == NULL) {
    printf("Failed to open file for writing: %s\n", path);
    return 1;
  }

  written = fwrite(key, len, 1, f);
  if (written != 1) {
    printf("Failed writing to file: %s\n", path);
    printf("Written: %ld\n", written);
    return 1;
  }
  if (fclose(f) != 0) {
    printf("Failed to close file stream: %s\n", path);
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  unsigned char pubkey[crypto_box_PUBLICKEYBYTES];
  unsigned char privkey[crypto_box_SECRETKEYBYTES];
  char *path = NULL;
  char *pubkey_path = NULL;
  char *privkey_path = NULL;
  char pubkey_file[] = "/key.pub";
  char privkey_file[] = "/key.priv";

  int c;

  while ((c = getopt(argc, argv, "p:")) != -1)
    switch (c) {
    case 'p':
      path = optarg;
      break;
    case '?':
      if (optopt == 'p')
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
      else if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      return 1;
    default:
      return 1;
    }

  if (path == NULL) {
    fprintf(stderr, "A path must be supplied with the -p argument\n");
    return 1;
  }

  if (sodium_init() < 0) {
    printf("%s\n", "Could init crypo lib. Exiting.");
    return 1;
  }

  crypto_box_keypair(pubkey, privkey);

  // size of path prefix plus length of filename plus one for string terminator
  pubkey_path = malloc(sizeof(char) * (strlen(path) + strlen(pubkey_file) + 1));
  privkey_path =
      malloc(sizeof(char) * (strlen(path) + strlen(privkey_file) + 1));

  strcat(pubkey_path, path);
  strcat(pubkey_path, pubkey_file);
  strcat(privkey_path, path);
  strcat(privkey_path, privkey_file);

  if (write_key(pubkey_path, pubkey, crypto_box_PUBLICKEYBYTES)) {
    free(pubkey_path);
    free(privkey_path);
    return 1;
  }

  if (write_key(privkey_path, privkey, crypto_box_SECRETKEYBYTES)) {
    free(pubkey_path);
    free(privkey_path);
    return 1;
  }

  free(pubkey_path);
  free(privkey_path);

  return 0;
}
