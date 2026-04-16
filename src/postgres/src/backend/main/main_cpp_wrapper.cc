extern "C" {
int PostgresServerProcessMain(int argc, char** argv);
}

int main(int argc, char** argv) {
	return PostgresServerProcessMain(argc, argv);
}
