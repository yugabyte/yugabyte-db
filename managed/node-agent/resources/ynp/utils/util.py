'''
Utility for defining the utility functions.
'''

import os
import tempfile


def safely_write_file(file_path, data):
    dir_name = os.path.dirname(file_path)
    temp_file = None
    try:
        # Create a temporary file in the same directory
        temp_file = tempfile.NamedTemporaryFile(delete=False, dir=dir_name)
        temp_file.write(data.encode())
        temp_file.flush()
        os.fsync(temp_file.fileno())
        temp_file.close()

        # Atomically rename the temporary file to the target file
        os.rename(temp_file.name, file_path)
        print("File written successfully and crash-consistent.")
    except IOError as e:
        print(f"An error occurred while writing the file: {e}")
        if temp_file is not None:
            os.unlink(temp_file.name)  # Clean up the temporary file
    finally:
        if temp_file is not None and not temp_file.closed:
            temp_file.close()
