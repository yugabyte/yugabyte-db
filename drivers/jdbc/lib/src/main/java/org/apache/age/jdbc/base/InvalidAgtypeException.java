package org.apache.age.jdbc.base;

/**
 * Runtime exception for when there is an invalid use of Agtype.
 */
public class InvalidAgtypeException extends RuntimeException {

    public InvalidAgtypeException(String message) {
        super(message);
    }

    public InvalidAgtypeException(String message, Throwable cause) {
        super(message, cause);
    }
}
