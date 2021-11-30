
from psycopg2.errors import *

class AgeNotSet(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'AGE extension is not set.' 

class GraphNotFound(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'Graph[' + self.name + '] does not exist.' 


class GraphAlreadyExists(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'Graph[' + self.name + '] already exists.' 

        
class GraphNotSet(Exception):
    def __repr__(self) :
        return 'Graph name is not set.'


class NoConnection(Exception):
    def __repr__(self) :
        return 'No Connection'

class NoCursor(Exception):
    def __repr__(self) :
        return 'No Cursor'

class SqlExcutionError(Exception):
    def __init__(self, msg, cause):
        self.msg = msg
        self.cause = cause
        super().__init__(msg, cause)
    
    def __repr__(self) :
        return 'SqlExcution [' + self.msg + ']'  

class AGTypeError(Exception):
    def __init__(self, msg, cause):
        self.msg = msg
        self.cause = cause
        super().__init__(msg, cause)


