import os

#sqalchemy is a Python tool to interact with databased without writing SQL queries
from sqlalchemy import create_engine
from sqlalchemy.orm import DeclarativeBase, sessionmaker


#Read the PostgreSQL connection URL from Render's environment variables.
#this is the DATABASE_URL that is set in the Render dashboard
database_url = os.environ["DATABASE_URL"]


#Tell SQLAlchemy to use the installed Psycopg driver. (psycopg is a PostgreSQL database adapter for Python)
#Render will provide a URL beginning with postgresql://.
database_url = database_url.replace(
    "postgresql://",
    "postgresql+psycopg://",
    1,
)

#Create the main connection between SQLAlchemy and PostgreSQL
#this is how SQLAlchemy reads and writes data to the database
engine = create_engine(database_url)

#Creates database sessions
#Each API request uses a session to read or write database data
SessionLocal = sessionmaker(bind=engine)


#All database table classes will inherit from Base
class Base(DeclarativeBase):
    pass


#Opens a database session for an API request and closes it when the request is finished
def get_db():
    db = SessionLocal()

    try:
        #Send the database session to the endpoint.
        yield db
    finally:
        #Always close the session when the request finishes.
        db.close()